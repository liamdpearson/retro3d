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

Asset paths are **relative to the working directory**, so always launch from the repo root (e.g. `assets/gun/gun.obj`, `assets/knight/knight2.fbx`).

Diagnostic `fprintf(stderr, ...)` output is **block-buffered when redirected to a file** on this MinGW setup and won't appear until the process exits — the render loop never exits, so run in a real terminal to see it live.

## Layout

- `src/main.cpp` — the *application*: scene setup, the render loop, and input callbacks. Owns the `parents` scene-root list.
- `src/graphics.cpp` — the *engine*: window/GL init, shaders, texture/model loaders, skinning/animation, and all draw logic.
- `src/graphics.h` — shared declarations plus the `Transform`, `Skeleton`, `BoneTrack`, `Animation`, and `Object` structs.
- `third_party/` — vendored deps, no package manager: `glad` (GL loader, `glad.c` compiled in), `glfw` (static `libglfw3.a`), `glm` (header-only), `stb` (image loading), `ufbx` (FBX loading, `ufbx.c` compiled in). Include dirs and the source list live in the `Makefile`.

## Architecture (the big picture)

**Global engine state, not objects.** Camera vectors, the window, the shader program, and the uniform locations (`modelLoc`/`viewLoc`/`projectionLoc`/`boneMatricesLoc`) are plain globals — `extern` in `graphics.h`, defined in `graphics.cpp`. The scene root list `parents` is defined in `main.cpp`. This is deliberate; don't refactor it into classes without reason.

**Fixed vertex format: 16 interleaved floats per vertex** (`VERTEX_FLOATS` in `graphics.h`), in this order:

| offset | count | attribute |
|--------|-------|-----------|
| 0  | 3 | position `x, y, z` |
| 3  | 2 | UV `u, v` |
| 5  | 3 | normal `nx, ny, nz` |
| 8  | 4 | bone indices (stored as floats, cast to int in the shader) |
| 12 | 4 | bone weights |

This is load-bearing. `uploadObject()` hardcodes stride `VERTEX_FLOATS * sizeof(float)` and sets up attribute slots 0–4 to match the table above. Every mesh source (`loadOBJ`, `loadFBX`, `makeSprite`) must emit exactly this layout — unskinned meshes just leave the bone indices/weights zeroed. Changing the format means editing this table, `uploadObject()`'s attribute pointers, **and** the vertex shader in lockstep. Normals are written but nothing reads them yet (no lighting).

**`Object` = CPU mesh + GL handles + a node in a scene graph.** It holds `vertices`/`indices`, GL `VAO/VBO/EBO/texture`, a `Transform`, a cached `world` mat4, `children`, and (for skinned FBX) a `Skeleton` plus a list of baked `animations`. `Object::Upload()` uploads itself then recurses into children. `Object::Draw()` draws itself, then for each child sets `child->world = this->world * child->transform.matrix()` and recurses — that's the parent→child transform composition.

**Transforms are kept as matrices to dodge gimbal lock.** `Transform` is position + `yaw` + `pitch` + uniform `scale`, and `Transform::matrix()` builds `T * R(yaw) * R(pitch) * S`. Composed world transforms are stored as `glm::mat4` and never decomposed back to Euler angles, which preserves roll from nested rotations. Keep it that way.

**Skinning & animation.** Skinned meshes carry a `Skeleton` (inverse-bind matrices, parent indices, per-bone `parentWorld` seed) and a `std::vector<Animation>` — one `Animation` per FBX anim stack, each a set of per-bone `BoneTrack`s (baked pos/rot/scale keyframes). `Object::currentAnim` selects the active clip and `Object::animTime` tracks playback. `computePose()` samples the current clip at `animTime`, blends the two nearest frames (lerp pos/scale, slerp rot), walks the bone hierarchy to world space, multiplies by the inverse bind, and uploads the resulting palette to `boneMatrices[]`. The vertex shader does linear-blend skinning; verts with zero total weight take a pass-through branch, so unskinned meshes are unaffected by stale palette data.

**Selecting a clip:** call `Object::SetAnimation(int index)` or `Object::SetAnimation(const std::string& name)` (matches the FBX anim-stack name; returns `false` if no clip matches). Both reset `animTime` so the new clip starts from frame 0. Objects default to `currentAnim = 0`. Switching is instant — there is no cross-fade blending between clips yet.

**Per-frame flow (in `main.cpp`'s loop):** `clearBG()` sets the viewport/scissor, builds `projection` and `view` from the camera globals, and uploads them once. Then for each parent: `obj->world = obj->transform.matrix()` followed by `obj->Draw()`. `drawObj()` advances `animTime`, uploads the per-object `model` matrix and (for skinned meshes) the bone palette, then issues `glDrawElements`.

**Rendering specifics:** one shader program with inline GLSL (string literals in `graphics.cpp`). Textured only — **no lighting**, even though normals are in the vertex format. The fragment shader does a Doom-style alpha-cutout (`discard` when `a < 0.5`). Sprites (`makeSprite`, `billboard = true`) get a camera-facing model matrix rebuilt each frame in `billboardModel()`.

**Camera:** FPS-style; mouse drives `yaw`/`pitch` → `cameraFront`. `E` toggles edit vs game mode (separate saved camera states); `P` pauses; WASD/Space/Shift move in edit mode.

## FBX loading (ufbx) gotchas

- Source files include **`ufbx.h`**, never `ufbx.c`. `ufbx.c` is compiled exactly once via the Makefile `SRCS`. Including `ufbx.c` in a `.cpp` double-defines every symbol → multiple-definition link errors.
- `loadFBX()` iterates scene **nodes** and bakes each `node->geometry_to_world` into the vertex positions. That matrix carries ufbx's unit (cm→m) and axis (Z-up→Y-up) conversion set via the load opts; reading `mesh->vertex_position` raw would skip it and yield ~100× oversized, wrongly-oriented geometry.
- Some exports load fine but contain no mesh nodes (`0 verts`). That's a bad export, not a loader bug — try re-exporting or a different file.
- **Skinning:** bone indices are collected from the mesh's first `skin_deformer`; each cluster maps to a global bone slot. Inverse-bind is stored as `geometry_to_bone * inverse(geometry_to_world)` so it composes correctly with the baked-in geometry transform. Only the top 4 weights per vertex are kept and renormalized.
- **Animation:** `loadFBX()` bakes **every** `scene->anim_stacks` entry into its own `Animation` (all sharing the same bone list), sampling each bone's local transform at 30 fps across the stack's time range via `ufbx_evaluate_transform`. Clip names come straight from the anim-stack names — check the `baked clip[i] '<name>'` stderr lines for the exact strings to pass to `SetAnimation`. Mixamo often names the stack `"mixamo.com"`, so index-based selection can be more practical for single-take exports.

## To-Do:
Add baked static lighting like in half life 1.


## Remember:
ufbx_transform_direction applies the full matrix, which is only correct for normals under rotation/uniform scale. Non-uniform scale needs the inverse-transpose. Fine for Stage 2 since nothing reads normals yet — flag it for when you add lighting.
