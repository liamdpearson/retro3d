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

Asset paths are **relative to the working directory**, so always launch from the repo root (e.g. `assets/gun/gun.obj`, `assets/audi/audi.fbx`).

Diagnostic `fprintf(stderr, ...)` output is **block-buffered when redirected to a file** on this MinGW setup and won't appear until the process exits — the render loop never exits, so run in a real terminal to see it live.

## Layout

- `src/main.cpp` — the *application*: scene setup, the render loop, and input callbacks. Owns the `parents` scene-root list.
- `src/graphics.cpp` — the *engine*: window/GL init, shaders, texture/model loaders, and all draw logic.
- `src/graphics.h` — shared declarations plus the `Transform` and `Object` structs.
- `third_party/` — vendored deps, no package manager: `glad` (GL loader, `glad.c` compiled in), `glfw` (static `libglfw3.a`), `glm` (header-only), `stb` (image loading), `ufbx` (FBX loading, `ufbx.c` compiled in). Include dirs and the source list live in the `Makefile`.

## Architecture (the big picture)

**Global engine state, not objects.** Camera vectors, the window, the shader program, and the matrix uniform locations (`modelLoc`/`viewLoc`/`projectionLoc`) are plain globals — `extern` in `graphics.h`, defined in `graphics.cpp`. The scene root list `parents` is defined in `main.cpp`. This is deliberate; don't refactor it into classes without reason.

**Fixed vertex format: 5 interleaved floats per vertex — `x, y, z, u, v`.** This is load-bearing. `uploadObject()` hardcodes stride `5 * sizeof(float)` with attribute 0 = position (3) and attribute 1 = UV (2). Both `loadOBJ()` and `loadFBX()` must emit exactly this layout. Any new mesh source, or adding normals/bone weights, means changing this format *and* `uploadObject()`'s attribute pointers *and* the vertex shader in lockstep.

**`Object` = CPU mesh + GL handles + a node in a scene graph.** It holds `vertices`/`indices`, GL `VAO/VBO/EBO/texture`, a `Transform`, a cached `world` mat4, and `children`. `Object::Upload()` uploads itself then recurses into children. `Object::Draw()` draws itself, then for each child sets `child->world = this->world * child->transform.matrix()` and recurses — that's the parent→child transform composition.

**Transforms are kept as matrices to dodge gimbal lock.** `Transform` is position + `yaw` + `pitch` + uniform `scale`, and `Transform::matrix()` builds `T * R(yaw) * R(pitch) * S`. Composed world transforms are stored as `glm::mat4` and never decomposed back to Euler angles, which preserves roll from nested rotations. Keep it that way.

**Per-frame flow (in `main.cpp`'s loop):** `clearBG()` sets the viewport/scissor, builds `projection` and `view` from the camera globals, and uploads them once. Then for each parent: `obj->world = obj->transform.matrix()` followed by `obj->Draw()`. `drawObj()` uploads the per-object `model` matrix and issues `glDrawElements`.

**Rendering specifics:** one shader program with inline GLSL (string literals in `graphics.cpp`). Textured only — **no lighting or normals**. The fragment shader does a Doom-style alpha-cutout (`discard` when `a < 0.5`). Sprites (`makeSprite`, `billboard = true`) get a camera-facing model matrix rebuilt each frame in `billboardModel()`.

**Camera:** FPS-style; mouse drives `yaw`/`pitch` → `cameraFront`. `E` toggles edit vs game mode (separate saved camera states); `P` pauses; WASD/Space/Shift move in edit mode.

## FBX loading (ufbx) gotchas

- Source files include **`ufbx.h`**, never `ufbx.c`. `ufbx.c` is compiled exactly once via the Makefile `SRCS`. Including `ufbx.c` in a `.cpp` double-defines every symbol → multiple-definition link errors.
- `loadFBX()` iterates scene **nodes** and bakes each `node->geometry_to_world` into the vertex positions. That matrix carries ufbx's unit (cm→m) and axis (Z-up→Y-up) conversion set via the load opts; reading `mesh->vertex_position` raw would skip it and yield ~100× oversized, wrongly-oriented geometry.
- Some exports load fine but contain no mesh nodes (`0 verts`). That's a bad export, not a loader bug — try re-exporting or a different file.
- This currently loads **static geometry only**. Skeletons, skinning, and animation are not implemented yet.

## Roadmap: skeletal animation

Getting animated characters is a staged effort. FBX parsing (done) is the easy part; skeletal animation is the real subsystem. Do these in order — each builds on the last.

1. **Stage 2 — Expand the vertex format for skinning.** Grow the fixed 5-float (pos+uv) layout to add `boneIndices` (ivec4), `boneWeights` (vec4), and `normal` (vec3). Update `uploadObject()`'s stride and attribute pointers, and the vertex shader's `layout` locations, in lockstep (see the "Fixed vertex format" note above). Have `loadFBX()` fill the new attributes (normals now; weights/ids can be zero until Stage 3).
2. **Stage 3 — Skeleton + linear-blend skinning shader.** Add a `Skeleton` (bone hierarchy + per-bone inverse bind matrices) alongside `Object`, populated from the FBX skin clusters. Rewrite the vertex shader to take `uniform mat4 boneMatrices[N]` and blend `boneWeights[i] * boneMatrices[boneIndices[i]]`. Upload a bind-pose palette and confirm the rest pose still renders.
3. **Stage 4 — Animation clip sampling.** Store per-bone position/rotation/scale keyframes from the FBX. Each frame: sample the clip, compose local bone transforms, walk the hierarchy to world-space matrices, multiply by inverse bind matrices, and upload the palette. Use `glm::quat` + `slerp` for rotations (keeps the matrix-based, gimbal-lock-free approach).


## Remember:
ufbx_transform_direction applies the full matrix, which is only correct for normals under rotation/uniform scale. Non-uniform scale needs the inverse-transpose. Fine for Stage 2 since nothing reads normals yet — flag it for when you add lighting.

Space mismatch. We bake geometry_to_world into positions but store ufbx's raw geometry_to_bone as the inverse bind. That's invisible now (identity palette), but Stage 4's non-identity palette will need to reconcile the two (either store geometry_to_bone · geometry_to_world⁻¹, or stop baking positions for skinned meshes).
