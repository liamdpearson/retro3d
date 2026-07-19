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

Asset paths are **relative to the working directory**, so always launch from the repo root (e.g. `assets/gun/gun.obj`, `assets/knight/knight2.fbx`). Meshes are built by `makeObj()` (OBJ, takes an `isStatic` flag) and `makeFbx()` (FBX, always dynamic); both return a `Mesh` by value.

Diagnostic `fprintf(stderr, ...)` output is **block-buffered when redirected to a file** on this MinGW setup and won't appear until the process exits — the render loop never exits, so run in a real terminal to see it live.

## Layout

- `src/main.cpp` — the *application*: scene setup, the render loop, and input handling. Owns the `camera`, the `parents` scene-root list, and the `lights` list.
- `src/graphics.cpp` — the *engine*: window/GL init, shaders, texture/model loaders, skinning/animation, lighting bake, and all draw logic.
- `src/graphics.h` — shared declarations plus the `Transform`, `Skeleton`, `BoneTrack`, `Animation`, `Light`, `Object`, `Mesh`, and `Camera` structs.
- `third_party/` — vendored deps, no package manager: `glad` (GL loader, `glad.c` compiled in), `glfw` (static `libglfw3.a`), `glm` (header-only), `stb` (image loading), `ufbx` (FBX loading, `ufbx.c` compiled in). Include dirs and the source list live in the `Makefile`.

## Architecture (the big picture)

**Global engine state, not objects.** The window, the shader program, the uniform locations (`modelLoc`/`viewLoc`/`projectionLoc`/`boneMatricesLoc`/`lightModeLoc`/`objectLightLoc`), the occluder list, and the light grid are plain globals — `extern` in `graphics.h`, defined in `graphics.cpp`. The scene root list `parents`, the `lights` list, and `camera` are defined in `main.cpp`. This is deliberate; don't refactor it into classes without reason.

Note that `yaw`, `pitch`, `lastX`, `lastY`, and `firstMouse` are still globals in `graphics.cpp`, but only the mouse-tracking three are live — `yaw`/`pitch` are vestigial from before the camera became a scene node and drive nothing.

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

Baked light **defaults to `1.0, 1.0, 1.0`, not zero** — it multiplies the texture, so an unbaked mesh renders fullbright and visible rather than black and invisible. Dynamic meshes never get baked and keep this default; the shader ignores it for them (see below).

Note that bumping `VERTEX_FLOATS` without emitting the matching floats in a loader is a *silent* failure: nothing fails to compile, the stride just walks out of step with the data and that one mesh renders as garbage.

## The scene graph: Object / Mesh / Camera

**`Object` is a bare scene node** — a `Transform`, a cached `world` mat4, and `children`. It carries no geometry, so a plain `Object` is a pivot / attachment point / group whose transform still composes into everything beneath it.

**Every walk over the graph is a virtual pair.** The base `Object` method does the recursion and nothing else; the derived type overrides it to do its own work and then chains to the base. That way a mesh-less node is never a special case at the call site. The four walks are `Upload()`, `Compose()`, `Draw()`, and the two bake passes `CollectOccluders()` / `BakeLighting()`.

**`Mesh : Object`** adds the CPU mesh (`vertices`/`indices`), the GL handles (`VAO/VBO/EBO/texture`, `indexCount`), the `isStatic` flag (which lighting path it takes), and — for skinned FBX — a `Skeleton` plus a list of baked `animations`. Its destructor frees the GL handles, which is why `~Object()` is virtual.

**`Camera : Object`** holds `FOV` plus derived `pos`/`front`/`up`. Because it's a node, it can be parented to a mover *and* have children of its own — `main.cpp` pushes `camera` into `parents` and parents the gun to it, which is how the viewmodel follows the view for free. `Camera::Compose()` reads `pos`/`front`/`up` straight out of the composed `world` matrix (via `mat3(world)` to drop translation, then normalize — a scaled parent leaks into the basis) rather than rebuilding them from yaw/pitch, so the camera inherits parent rotation including roll. **The camera must be reachable from `parents`** or `Compose()` never runs on it and the view silently freezes at whatever the constructor set.

**Transforms are kept as matrices to dodge gimbal lock.** `Transform` is position + `yaw` + `pitch` + uniform `scale`, and `Transform::matrix()` builds `T * R(yaw) * R(-pitch) * S`. Composed world transforms are stored as `glm::mat4` and never decomposed back to Euler angles, which preserves roll from nested rotations. Keep it that way. `Transform::scale` defaults to 1, never 0 — a zero there silently collapses every descendant to a point.

**Compose is a separate pass from Draw, and the order matters.** `Object::Compose()` sets `child->world = this->world * child->transform.matrix()` down the graph. It is deliberately *not* fused into `Draw()`: the camera derives `pos`/`front` during Compose, and `clearBG()` needs those to build the view matrix before the first mesh is drawn. Fusing them made the view matrix trail the scene by one frame (fixed in `c2c029e`).

## Per-frame flow (`main.cpp`'s loop)

1. `glfwPollEvents()` **first**, so the frame acts on this frame's input.
2. Update `deltaTime`, read keys, mutate transforms (WASD/Space/Shift move `camera.transform`; `Escape` quits; `H` prints the camera position).
3. For each root: `obj->world = obj->transform.matrix()` then `obj->Compose()`.
4. `clearBG()` — sets viewport/scissor, builds `projection` and `view` from the camera, uploads them once.
5. For each root: `obj->Draw()`.

`drawObj()` advances `animTime`, uploads the per-mesh `model` matrix, sets `LightMode` (and `ObjectLight` for movers), uploads the bone palette for skinned meshes, then issues `glDrawElements`.

**Camera input:** FPS-style. `mouseCallback` drives `camera.transform.yaw`/`pitch` directly and clamps pitch to ±89°. There is no edit/game mode toggle and no pause key.

## Lighting

Baked, Half-Life 1 style — two paths chosen by `Mesh::isStatic`. A `Light` is a point light: `pos`, `color`, `intensity`, `radius`. The scene's lights live in the `lights` list in `main.cpp`. There is no realtime per-pixel lighting; the fragment shader just multiplies the texture by a light value and the two paths differ only in where that value comes from (`uniform int LightMode`: 0 = baked per-vertex, 1 = per-object).

**The attenuation curve is duplicated between `sampleLightAt()` and `Mesh::BakeLighting()` and must stay identical**, or movers and the baked floor beneath them will disagree about how bright the room is. Both use `intensity / (1 + d² / radius)` — the `1 +` keeps a light sitting exactly on a vertex from dividing by zero. Not physical, but stable and easy to tune.

`bakeSceneLighting()` is called after the scene graph is assembled and the lights are placed but **before** `Upload()`. It runs three stages:

1. **Collect occluders.** `CollectOccluders()` flattens every static triangle in the graph into the world-space global `occluders`. Dynamic meshes are excluded: they move, and a shadow baked from them wouldn't. The list is a global and stays alive past the bake, because the grid stage and `sampleLightAt()` both need it.
2. **Bake static vertices.** `BakeLighting()` walks the graph and, per vertex per light, accumulates `ambient + lambert × attenuation × color`, firing a shadow ray (`rayOccluded()`, Möller–Trumbore, two-sided, first-hit) and skipping any light that's blocked. The result is clamped and written into vertex floats 16–18.
3. **Build the light grid.** The occluder triangles are reduced to a world AABB (`minX`…`maxZ`, snapped outward to integers), and `sampleLightAt()` is evaluated at every integer point inside it into `lightGrid`. Storage order is x outer, z inner, so a cell lives at `(gx * ny + gy) * nz + gz`.

Both bake passes take `parentWorld` **explicitly** rather than reading `world`, because the bake runs before the first `Draw()`/`Compose()` — only roots have a valid `world` at that point and every child's is still identity.

> **Gotcha:** in the `Mesh` overrides, the chain to `Object::CollectOccluders` / `Object::BakeLighting` passes **`parentWorld`, not the composed `world`**. The base recomposes this node's transform itself, so passing `world` applies the mesh's transform twice to every descendant — silent, and geometrically plausible enough to miss.

Because the static result is folded into the vertex data, **a baked mesh must never move afterwards.** The bake is naive `O(verts × lights × tris)` plus `O(cells × lights × tris)` for the grid; it prints its triangle count and elapsed time to stderr. A spatial grid over the occluders is the fix if it ever gets slow.

`SHADOW_BIAS` (`graphics.h`) lifts each ray off the surface it starts on — without it every vertex re-hits its own triangles and the scene self-shadows into speckle. It's tuned for a metre-scale scene.

**Dynamic meshes** take one light value for the whole mesh, looked up per draw from the light grid by `gridLightAt()` at the mesh's world origin (column 3 of `world`, no decompose needed). `gridLightAt()` trilinearly blends the eight surrounding cells so a mover crossing a cell boundary fades instead of popping, clamps out-of-bounds points to the edge cell in *grid space* (before splitting into index and fraction, so weights agree with the clamped indices), and returns fullbright if the grid is empty — no bake means visible, not black.

The remaining asymmetry is that a mover gets no lambert term, so it shades uniformly and reads flatter than static geometry beside it. Shadowing is *not* on that list anymore: because `sampleLightAt()` shadow-tests against the persistent `occluders` list, the grid carries shadow information and a mover passing behind geometry does darken.

## Rendering specifics

One shader program with inline GLSL (string literals in `graphics.cpp`). The vertex shader does linear-blend skinning; verts with zero total weight take a pass-through branch, so unskinned meshes are unaffected by stale palette data. The fragment shader does a Doom-style alpha-cutout (`discard` when `a < 0.5`). Shaders compile at *runtime*, so GLSL typos survive `mingw32-make` and only surface as a compile log on launch. `MAX_BONES` is declared in both the C++ header and the shader source and must be kept in sync.

## Skinning & animation

Skinned meshes carry a `Skeleton` (inverse-bind matrices, parent indices, per-bone `parentWorld` seed) and a `std::vector<Animation>` — one `Animation` per FBX anim stack, each a set of per-bone `BoneTrack`s (baked pos/rot/scale keyframes). `Mesh::currentAnim` selects the active clip and `Mesh::animTime` tracks playback. `computePose()` samples the current clip at `animTime`, blends the two nearest frames (lerp pos/scale, slerp rot), walks the bone hierarchy to world space, multiplies by the inverse bind, and uploads the resulting palette to `boneMatrices[]`.

**Selecting a clip:** call `Mesh::SetAnimation(int index)` or `Mesh::SetAnimation(const std::string& name)` (matches the FBX anim-stack name; returns `false` if no clip matches). Both reset `animTime` so the new clip starts from frame 0. Meshes default to `currentAnim = 0`. Switching is instant — there is no cross-fade blending between clips yet.

## FBX loading (ufbx) gotchas

- Source files include **`ufbx.h`**, never `ufbx.c`. `ufbx.c` is compiled exactly once via the Makefile `SRCS`. Including `ufbx.c` in a `.cpp` double-defines every symbol → multiple-definition link errors.
- `loadFBX()` iterates scene **nodes** and bakes each `node->geometry_to_world` into the vertex positions. That matrix carries ufbx's unit (cm→m) and axis (Z-up→Y-up) conversion set via the load opts; reading `mesh->vertex_position` raw would skip it and yield ~100× oversized, wrongly-oriented geometry.
- Some exports load fine but contain no mesh nodes (`0 verts`). That's a bad export, not a loader bug — try re-exporting or a different file.
- **Skinning:** bone indices are collected from the mesh's first `skin_deformer`; each cluster maps to a global bone slot. Inverse-bind is stored as `geometry_to_bone * inverse(geometry_to_world)` so it composes correctly with the baked-in geometry transform. Only the top 4 weights per vertex are kept and renormalized.
- **Animation:** `loadFBX()` bakes **every** `scene->anim_stacks` entry into its own `Animation` (all sharing the same bone list), sampling each bone's local transform at 30 fps across the stack's time range via `ufbx_evaluate_transform`. Clip names come straight from the anim-stack names — check the `baked clip[i] '<name>'` stderr lines for the exact strings to pass to `SetAnimation`. Mixamo often names the stack `"mixamo.com"`, so index-based selection can be more practical for single-take exports.

## To-Do

- Dynamic meshes are lit flat — one value across the whole mesh, so characters have no internal shading. The fix is a dominant light direction out of the grid lookup plus a lambert term against `Normal` in the fragment shader (the varying is already plumbed through for this).
- Baked lighting is per-vertex, so its resolution is tied to mesh tessellation: a large untessellated face gets one value per corner and shadow edges land on triangle boundaries. Subdividing big static faces at bake time helps; lightmaps are the real upgrade, and `bakeSceneLighting()` is mostly reusable for them.
- The light grid is fixed at 1-unit spacing, which is coarse for a metre-scale scene and unbounded in cost for a large one. Configurable spacing (or a sparse/hierarchical grid) is the next step.
- Shadow rays treat alpha-cutout geometry as solid, so a cutout texture casts the shadow of its full quad.
- `bakeSceneLighting()` still prints its AABB with raw `std::cout` debug lines; those should go or become proper stderr diagnostics.
- Animation clip switching is instant — no cross-fade.

## Remember

`ufbx_transform_direction` applies the full matrix, which is only correct for normals under rotation/uniform scale — non-uniform scale needs the inverse-transpose. `loadFBX` still uses it, so FBX normals are wrong under non-uniform scale. `Mesh::BakeLighting()` sidesteps this on its own side by using the inverse-transpose of the world matrix, but that can't repair a normal that was already mangled at load time.
