# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A small, from-scratch OpenGL 3.3 (core profile) 3D game engine written as a learning project. Windows + MinGW `g++`, driven from Git Bash. No engine framework, no package manager — everything is hand-written or vendored.

## Build & run

Run from **Git Bash** at the repo root:

- `mingw32-make` — build `main.exe`
- `mingw32-make run` — build + launch
- `mingw32-make clean` — remove the exe

There is **no test suite and no linter**. Correctness is checked by running the app and looking. The build passes `-Wall -Wextra`; unused-parameter warnings in `main.cpp` callbacks are known/expected.

Asset paths are **relative to the working directory**, so always launch from the repo root (e.g. `assets/gun/gun.obj`, `assets/knight/knight2.fbx`). Objects are built by `makeObj()` (OBJ, takes an `isStatic` flag) and `makeFbx()` (FBX, always dynamic).

Diagnostic `fprintf(stderr, ...)` output is **block-buffered when redirected to a file** on this MinGW setup and won't appear until the process exits — the render loop never exits, so run in a real terminal to see it live.

## Layout

- `src/main.cpp` — the *application*: scene setup, the render loop, and input callbacks. Owns the `parents` scene-root list and the `lights` list.
- `src/graphics.cpp` — the *engine*: window/GL init, shaders, texture/model loaders, skinning/animation, and all draw logic.
- `src/graphics.h` — shared declarations plus the `Transform`, `Skeleton`, `BoneTrack`, `Animation`, `Light`, and `Object` structs.
- `third_party/` — vendored deps, no package manager: `glad` (GL loader, `glad.c` compiled in), `glfw` (static `libglfw3.a`), `glm` (header-only), `stb` (image loading), `ufbx` (FBX loading, `ufbx.c` compiled in). Include dirs and the source list live in the `Makefile`.

## Architecture (the big picture)

**Global engine state, not objects.** Camera vectors, the window, the shader program, and the uniform locations (`modelLoc`/`viewLoc`/`projectionLoc`/`boneMatricesLoc`) are plain globals — `extern` in `graphics.h`, defined in `graphics.cpp`. The scene root list `parents` is defined in `main.cpp`. This is deliberate; don't refactor it into classes without reason.

**Fixed vertex format: 19 interleaved floats per vertex** (`VERTEX_FLOATS` in `graphics.h`), in this order:

| offset | count | attribute |
|--------|-------|-----------|
| 0  | 3 | position `x, y, z` |
| 3  | 2 | UV `u, v` |
| 5  | 3 | normal `nx, ny, nz` |
| 8  | 4 | bone indices (stored as floats, cast to int in the shader) |
| 12 | 4 | bone weights |
| 16 | 3 | baked light color `r, g, b` |

This is load-bearing. `uploadObject()` hardcodes stride `VERTEX_FLOATS * sizeof(float)` and sets up attribute slots 0–5 to match the table above. Every mesh source (`loadOBJ`, `loadFBX`) must emit exactly this layout — unskinned meshes just leave the bone indices/weights zeroed. Changing the format means editing this table, `uploadObject()`'s attribute pointers, **and** the vertex shader in lockstep.

Baked light **defaults to `1.0, 1.0, 1.0`, not zero** — it multiplies the texture, so an unbaked mesh renders fullbright and visible rather than black and invisible. Dynamic objects never get baked and keep this default; the shader ignores it for them (see below).

Note that bumping `VERTEX_FLOATS` without emitting the matching floats in a loader is a *silent* failure: nothing fails to compile, the stride just walks out of step with the data and that one mesh renders as garbage.

**`Object` = CPU mesh + GL handles + a node in a scene graph.** It holds `vertices`/`indices`, GL `VAO/VBO/EBO/texture`, a `Transform`, a cached `world` mat4, `children`, an `isStatic` flag (which lighting path it takes), and (for skinned FBX) a `Skeleton` plus a list of baked `animations`. `Object::Upload()` uploads itself then recurses into children. `Object::Draw()` draws itself, then for each child sets `child->world = this->world * child->transform.matrix()` and recurses — that's the parent→child transform composition.

**Transforms are kept as matrices to dodge gimbal lock.** `Transform` is position + `yaw` + `pitch` + uniform `scale`, and `Transform::matrix()` builds `T * R(yaw) * R(pitch) * S`. Composed world transforms are stored as `glm::mat4` and never decomposed back to Euler angles, which preserves roll from nested rotations. Keep it that way.

**Skinning & animation.** Skinned meshes carry a `Skeleton` (inverse-bind matrices, parent indices, per-bone `parentWorld` seed) and a `std::vector<Animation>` — one `Animation` per FBX anim stack, each a set of per-bone `BoneTrack`s (baked pos/rot/scale keyframes). `Object::currentAnim` selects the active clip and `Object::animTime` tracks playback. `computePose()` samples the current clip at `animTime`, blends the two nearest frames (lerp pos/scale, slerp rot), walks the bone hierarchy to world space, multiplies by the inverse bind, and uploads the resulting palette to `boneMatrices[]`. The vertex shader does linear-blend skinning; verts with zero total weight take a pass-through branch, so unskinned meshes are unaffected by stale palette data.

**Selecting a clip:** call `Object::SetAnimation(int index)` or `Object::SetAnimation(const std::string& name)` (matches the FBX anim-stack name; returns `false` if no clip matches). Both reset `animTime` so the new clip starts from frame 0. Objects default to `currentAnim = 0`. Switching is instant — there is no cross-fade blending between clips yet.

**Lighting is baked, Half-Life 1 style — two paths chosen by `Object::isStatic`.** A `Light` is a point light: `pos`, `color`, `intensity`. The scene's lights live in the `lights` list in `main.cpp`. There is no realtime per-pixel lighting; the fragment shader just multiplies the texture by a light value and the two paths differ only in where that value comes from (`uniform int LightMode`: 0 = baked, 1 = dynamic).

*Static objects* are baked once by `bakeSceneLighting()`, called after the scene graph is assembled and the lights are placed but **before** `Upload()`. It runs in two passes because occlusion is scene-global — one object's vertices must be testable against every other object's triangles:

1. `collectOccluders()` flattens every static triangle in the graph into a world-space list. Dynamic objects are excluded: they move, and a shadow baked from them wouldn't.
2. `bakeObjectLighting()` walks the graph again and, per vertex per light, accumulates `ambient + lambert × attenuation × color`, firing a shadow ray (`rayOccluded()`, Möller–Trumbore, two-sided, first-hit) and skipping any light that's blocked. The result is clamped and written into vertex floats 16–18.

Because the result is folded into the vertex data, **a baked object must never move afterwards.** The bake is naive `O(verts × lights × tris)`; it prints its triangle count and elapsed time to stderr. A spatial grid is the fix if it ever gets slow.

`SHADOW_BIAS` (`graphics.h`) lifts each ray off the surface it starts on — without it every vertex re-hits its own triangles and the scene self-shadows into speckle. It's tuned for a metre-scale scene.

*Dynamic objects* take one light value for the whole mesh, sampled at their world origin by `sampleLightAt()` and uploaded per draw. No normal term, so brightness falls off with distance alone and the mesh shades uniformly. **The attenuation curve is duplicated between `sampleLightAt()` and `bakeObjectLighting()` and must stay identical**, or movers and the baked floor beneath them will disagree about how bright the room is.

Two known asymmetries, both inherent to origin sampling and both also true of HL1: a mover gets no lambert and no shadow test, so it reads brighter than static geometry beside it and won't darken when it passes behind something. Fixing the latter means keeping the occluder list alive past the bake (it's currently local to `bakeSceneLighting()`).

**Per-frame flow (in `main.cpp`'s loop):** `clearBG()` sets the viewport/scissor, builds `projection` and `view` from the camera globals, and uploads them once. Then for each parent: `obj->world = obj->transform.matrix()` followed by `obj->Draw()`. `drawObj()` advances `animTime`, uploads the per-object `model` matrix, sets `LightMode` (and `ObjectLight` for movers), uploads the bone palette for skinned meshes, then issues `glDrawElements`.

**Rendering specifics:** one shader program with inline GLSL (string literals in `graphics.cpp`). The fragment shader does a Doom-style alpha-cutout (`discard` when `a < 0.5`). Shaders compile at *runtime*, so GLSL typos survive `mingw32-make` and only surface as a compile log on launch.

**Camera:** FPS-style; mouse drives `yaw`/`pitch` → `cameraFront`. `E` toggles edit vs game mode (separate saved camera states); `P` pauses; WASD/Space/Shift move in edit mode.

## FBX loading (ufbx) gotchas

- Source files include **`ufbx.h`**, never `ufbx.c`. `ufbx.c` is compiled exactly once via the Makefile `SRCS`. Including `ufbx.c` in a `.cpp` double-defines every symbol → multiple-definition link errors.
- `loadFBX()` iterates scene **nodes** and bakes each `node->geometry_to_world` into the vertex positions. That matrix carries ufbx's unit (cm→m) and axis (Z-up→Y-up) conversion set via the load opts; reading `mesh->vertex_position` raw would skip it and yield ~100× oversized, wrongly-oriented geometry.
- Some exports load fine but contain no mesh nodes (`0 verts`). That's a bad export, not a loader bug — try re-exporting or a different file.
- **Skinning:** bone indices are collected from the mesh's first `skin_deformer`; each cluster maps to a global bone slot. Inverse-bind is stored as `geometry_to_bone * inverse(geometry_to_world)` so it composes correctly with the baked-in geometry transform. Only the top 4 weights per vertex are kept and renormalized.
- **Animation:** `loadFBX()` bakes **every** `scene->anim_stacks` entry into its own `Animation` (all sharing the same bone list), sampling each bone's local transform at 30 fps across the stack's time range via `ufbx_evaluate_transform`. Clip names come straight from the anim-stack names — check the `baked clip[i] '<name>'` stderr lines for the exact strings to pass to `SetAnimation`. Mixamo often names the stack `"mixamo.com"`, so index-based selection can be more practical for single-take exports.

## To-Do:
- Dynamic objects are lit flat — one value across the whole mesh, so characters have no internal shading. The fix is a dominant light direction out of `sampleLightAt()` plus a lambert term against `Normal` in the fragment shader (the varying is already plumbed through for this).
- Baked lighting is per-vertex, so its resolution is tied to mesh tessellation: a large untessellated face gets one value per corner and shadow edges land on triangle boundaries. Subdividing big static faces at bake time helps; lightmaps are the real upgrade, and `bakeSceneLighting()` is mostly reusable for them.
- Shadow rays treat alpha-cutout geometry as solid, so a cutout texture casts the shadow of its full quad.


## Remember:
`ufbx_transform_direction` applies the full matrix, which is only correct for normals under rotation/uniform scale — non-uniform scale needs the inverse-transpose. `loadFBX` still uses it, so FBX normals are wrong under non-uniform scale. `bakeObjectLighting()` sidesteps this on its own side by using the inverse-transpose of the world matrix, but that can't repair a normal that was already mangled at load time.
