# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A small, from-scratch OpenGL 3.3 (core profile) 3D game engine written as a learning project, in the style of Half-Life 1 (baked lighting, alpha-cutout textures). Windows + MinGW `g++`, driven from Git Bash. No engine framework, no package manager — everything is hand-written or vendored.

Two executables share one engine design but **do not share code**: `game.exe` (plays a scene) and `editor.exe` (edits one — moves objects, tweaks lights, exports `scene.json`). See "The game/editor split" below before assuming a fix in one applies to the other.

## Build & run

Run from **Git Bash** at the repo root:

- `mingw32-make` — build both `game.exe` and `editor.exe`
- `mingw32-make game` / `mingw32-make editor` — build one target only
- `mingw32-make run` — build + launch `game.exe`
- `mingw32-make run-editor` — build + launch `editor.exe`
- `mingw32-make clean` — remove both exes

There is **no test suite and no linter**. Correctness is checked by running the app and looking. The build passes `-Wall -Wextra`; unused-parameter warnings in callbacks are known/expected.

Asset and scene paths are **relative to the working directory**, so always launch from the repo root (e.g. `assets/gun/gun.obj`, `scene.json`). Meshes are built by `makeObj()` (OBJ) and `makeFbx()` (FBX, skinning-capable); both return a mesh struct by value — see the game/editor split for why the exact type differs.

Diagnostic `fprintf(stderr, ...)` output is **block-buffered when redirected to a file** on this MinGW setup and won't appear until the process exits — the render loop never exits, so run in a real terminal to see it live.

## Layout

- `src/game/game.cpp` / `game.hpp` — the game *application*: scene import, the render loop, FPS-style input. Owns `camera`, `parents`, `lights`, `uiElements`.
- `src/game/graphics.cpp` / `graphics.hpp` — the game *engine*: window/GL init, shaders, texture/model loaders, skinning/animation, **lighting bake**, text/UI, and all draw logic.
- `src/editor/editor.cpp` / `editor.hpp` — the editor *application*: scene import/export, gizmo-based object/light editing, hierarchy + inspector text UI.
- `src/editor/editor_graphics.cpp` / `editor_graphics.hpp` — the editor's own engine copy: same shape as `src/game/graphics.*` minus the lighting bake, plus `Raycast()`-based picking and gizmo mesh factories.
- `third_party/` — vendored deps, no package manager: `glad` (GL loader, `glad.c` compiled in), `glfw` (static `libglfw3.a`), `glm` (header-only), `stb` (image loading), `ufbx` (FBX loading, `ufbx.c` compiled in), `nlohmann/json.hpp` (scene serialization, header-only).
- `assets/engine_assets/` — gizmo meshes the editor draws in place of data-only nodes (`camera/`, `light/`, `pivot/`), so lights and the camera start are visible and clickable in the editor. The game never touches this directory.
- `scene.json` — the scene definition both exes read (editor also writes it). See "Scene format" below.

## The game/editor split

**This is not a shared library with two `main()`s — `src/game/graphics.*` and `src/editor/editor_graphics.*` are two independent copies** of the same design, compiled and linked separately (see `Makefile`: each target lists its own two `.cpp` files plus the common vendored sources). A fix to a bug in the vertex format, the lighting math, or a loader almost certainly needs to be made **in both places**. Grep both `src/game/` and `src/editor/` before assuming a change is complete.

Why two copies instead of one: the editor needs things the game doesn't (raycast picking, gizmo meshes for otherwise-invisible nodes, scene export) and skips things the game needs (lighting bake — the editor always renders fullbright so placement isn't fighting stale baked shadows). Splitting kept each side simple rather than threading `#ifdef EDITOR` through one shared engine.

Known divergences between the two copies, worth checking before you rely on either:
- **Lights are represented differently.** In the game, a `"light"` scene node becomes a data-only `Light` (pos/color/intensity/radius) pushed onto the separate `lights` list — it is never a graph node and has no mesh. In the editor, a `"light"` node becomes a `LightMesh : Mesh` (carries color/intensity/radius *and* a visible gizmo mesh) and lives in `parents` like any other object, so it can be selected and dragged. `Object::isLight()` (editor only) distinguishes it at draw time.
- **`Object` carries a `type` string in the editor** (`editor_graphics.hpp`) but not in the game (`graphics.hpp`) — the editor needs it to round-trip a node back to JSON and to label the inspector; the game never serializes, so it never needed the field.
- **`makeFbx()`'s signature differs**: the editor's takes an explicit `isStatic` bool (`FbxMesh makeFbx(..., bool isStatic)`), the game's does not (`Mesh makeFbx(...)` — FBX meshes are implicitly dynamic there). If you add static-FBX support, this needs reconciling in both places, and note `Mesh::isStatic` is a field in the game's `Mesh` but not the editor's — the editor puts `isStatic` only on `ObjMesh`/`FbxMesh`.
- **The editor's `Object` has no `CollectOccluders()`/`BakeLighting()` virtuals** — those only exist in the game's engine, since the editor never bakes.

## Scene format (`scene.json`, see also `README.md`)

Top level is `{"scene": [...]}`, a list of nodes. Each node has a `"type"` and, except for lights, a 7-float `"transform"` array: `[x, y, z, yaw, pitch, roll, scale]`. Children nest under `"children"`. Supported types:

1. `"pivot"` — an invisible grouping node.
2. `"mesh-obj"` — static-capable geometry from an OBJ (`"obj src"`, `"tex src"`, `"isStatic"`). No animation.
3. `"mesh-fbx"` — geometry from an FBX (`"obj src"`, `"tex src"`); can carry baked animation clips.
4. `"light"` — a point light (`"position"`, `"color"`, `"intensity"`, `"radius"`). Cannot have children or be a child.
5. `"camera"` — one per scene; sets the game's starting camera transform/FOV (`"fov"`).

Both `buildNode()` (import, in `game.hpp`/`editor.hpp`) and, editor-side, `objectToJson()` (export, in `editor.hpp`) must agree on these fields — the export path packs a node's type-specific data (obj/tex src, isStatic, FOV, light color/intensity/radius) by `dynamic_cast`ing to the concrete mesh subtype, since none of that lives on the base `Object`.

## Architecture (the big picture, applies to both copies unless noted)

**Global engine state, not objects.** The window, the shader program, the uniform locations (`modelLoc`/`viewLoc`/`projectionLoc`/`boneMatricesLoc`/`lightModeLoc`/`objectLightLoc`/`textModeLoc`), and (game only) the occluder list and light grid are plain globals — `extern` in the `graphics.hpp` of each copy, defined in the matching `.cpp`. The scene root list `parents`, `camera`, and `uiElements` are defined in each app's `.hpp` (game also defines `lights` there). This is deliberate; don't refactor it into classes without reason.

Note that `yaw`, `pitch`, `lastX`, `lastY`, and `firstMouse` are still globals, but only the mouse-tracking three are live — `yaw`/`pitch` are vestigial from before the camera became a scene node and drive nothing.

**Fixed vertex format: 19 interleaved floats per vertex** (`VERTEX_FLOATS`), in this order:

| offset | count | attribute |
|--------|-------|-----------|
| 0  | 3 | position `x, y, z` |
| 3  | 2 | UV `u, v` |
| 5  | 3 | normal `nx, ny, nz` |
| 8  | 4 | bone indices (stored as floats, cast to int in the shader) |
| 12 | 4 | bone weights |
| 16 | 3 | baked light color `r, g, b` |

This is load-bearing. `uploadObject()` hardcodes stride `VERTEX_FLOATS * sizeof(float)` and sets up attribute slots 0–5 to match the table above. Every mesh source (`loadOBJ`, `loadFBX`) must emit exactly this layout — unskinned meshes just leave the bone indices/weights zeroed. Changing the format means editing this table, `uploadObject()`'s attribute pointers, **and** the vertex shader in lockstep — in both copies.

Baked light **defaults to `1.0, 1.0, 1.0`, not zero** — it multiplies the texture, so an unbaked mesh (or anything drawn by the editor, which never bakes) renders fullbright and visible rather than black and invisible. Dynamic meshes never get baked and keep this default; the shader ignores it for them (see Lighting below).

Bumping `VERTEX_FLOATS` without emitting the matching floats in a loader is a *silent* failure: nothing fails to compile, the stride just walks out of step with the data and that one mesh renders as garbage.

## The scene graph: Object / Mesh / Camera

**`Object` is a bare scene node** — a transform, a cached `world` mat4, and `children`. It carries no geometry, so a plain `Object` (game) or `Mesh` built from `pivot.obj` (editor — see below) is a pivot / attachment point / group whose transform still composes into everything beneath it.

**Every walk over the graph is a virtual pair.** The base `Object` method does the recursion and nothing else; the derived type overrides it to do its own work and then chains to the base. That way a mesh-less node is never a special case at the call site. The walks are `Upload()`, `Compose()`, `Draw()`, `Raycast()` (both copies), plus the two bake passes `CollectOccluders()` / `BakeLighting()` (game only).

**`Mesh : Object`** adds the CPU mesh (`vertices`/`indices`), the GL handles (`VAO/VBO/EBO/texture`, `indexCount`), and — for skinned FBX — a `Skeleton` plus a list of baked `animations`. Its destructor frees the GL handles, which is why `~Object()` is virtual. In the editor, `Mesh` is further subtyped per scene-node kind (`LightMesh`, `CameraMesh`, `ObjMesh`, `FbxMesh`) purely so `objectToJson()` can recover each type's fixed fields on export; in the game, one `Mesh` struct (with an `isStatic` flag) covers both OBJ and FBX since nothing round-trips back to JSON.

**`Camera : Object`** holds `FOV` plus derived `pos`/`front`/`up`. Because it's a node, it can be parented to a mover *and* have children of its own. `Camera::Compose()` reads `pos`/`front`/`up` straight out of the composed `world` matrix (via `mat3(world)` to drop translation, then normalize — a scaled parent leaks into the basis) rather than rebuilding them from yaw/pitch, so the camera inherits parent rotation including roll. **The camera must be reachable from `parents`** or `Compose()` never runs on it and the view silently freezes at whatever the constructor set. (In the editor, `main()` pushes `&camera` into `parents` explicitly after `importScene()`, since the scene's own `"camera"` node is a separate gizmo object, not the fly-camera.)

**Transforms are kept as matrices to dodge gimbal lock.** `Transform` is position + `yaw` + `pitch` + `roll` + uniform `scale`, and `Transform::matrix()` builds `T * R(yaw) * R(-pitch) * R(roll) * S`. Composed world transforms are stored as `glm::mat4` and never decomposed back to Euler angles, which preserves roll from nested rotations. Keep it that way. `Transform::scale` defaults to 1, never 0 — a zero there silently collapses every descendant to a point.

**Compose is a separate pass from Draw, and the order matters.** `Object::Compose()` sets `child->world = this->world * child->transform.matrix()` down the graph. It is deliberately *not* fused into `Draw()`: the camera derives `pos`/`front` during Compose, and `clearBG()` needs those to build the view matrix before the first mesh is drawn. Fusing them made the view matrix trail the scene by one frame.

## Per-frame flow (both apps' loops)

1. `glfwPollEvents()` **first**, so the frame acts on this frame's input.
2. Update `deltaTime`, read keys, mutate transforms (WASD/Space/Shift move `camera.transform`; `Escape` quits).
3. For each root: `obj->world = obj->transform.matrix()` then `obj->Compose()`.
4. `clearBG()` — sets viewport/scissor, builds `projection` and `view` from the camera, uploads them once.
5. For each root: `obj->Draw()`. Then `beginUI()` / draw `UIElement`s and `UIText`s / `endUI()`.

`drawObj()` advances `animTime`, uploads the per-mesh `model` matrix, sets `LightMode` (and `ObjectLight` for movers/light-gizmos), uploads the bone palette for skinned meshes, then issues `glDrawElements`.

**Game camera input:** FPS-style, no edit/game mode toggle, no pause key. `mouseCallback` drives `camera.transform.yaw`/`pitch` directly and clamps pitch to ±89°.

**Editor camera + editing input:** same fly camera, plus:
- Left-click: `raycast(camera.pos, camera.front, 100.0f)` against every mesh in `parents` and sets `curObject` to the hit (or `nullptr`).
- Number keys `1`–`7`/`8` (handled in `key_callback` → `handleOtherInput`/`handleLightInput`) point `curElement` (a `float*`) at one field of the selected object's transform (or, for lights, position/color/intensity/radius) and set `editMultiplier` for its sensitivity.
- Holding right mouse button while a field is armed feeds mouse-delta into `*curElement` inside `mouseCallback`, instead of turning the camera.
- `Ctrl+S` calls `exportScene("scene.json")`.
- The inspector (`curObjectLabel`) and scene hierarchy (`hierarchyLabel`, built by `buildHierarchyString()`) are drawn as screen-space `UIText` every frame.

## Lighting (game only — the editor never bakes)

Baked, Half-Life 1 style — two paths chosen by `Mesh::isStatic`. A `Light` is a point light: `pos`, `color`, `intensity`, `radius`. The scene's lights live in the `lights` list in `game.hpp`. There is no realtime per-pixel lighting; the fragment shader just multiplies the texture by a light value and the two paths differ only in where that value comes from (`uniform int LightMode`: 0 = baked per-vertex, 1 = per-object).

**The attenuation curve is duplicated between `sampleLightAt()` and `Mesh::BakeLighting()` and must stay identical**, or movers and the baked floor beneath them will disagree about how bright the room is. Both use `intensity / (1 + d² / radius)` — the `1 +` keeps a light sitting exactly on a vertex from dividing by zero. Not physical, but stable and easy to tune.

`bakeSceneLighting()` is called after the scene graph is assembled and the lights are placed but **before** `Upload()`. It runs three stages:

1. **Collect occluders.** `CollectOccluders()` flattens every static triangle in the graph into the world-space global `occluders`. Dynamic meshes are excluded: they move, and a shadow baked from them wouldn't. The list is a global and stays alive past the bake, because the grid stage and `sampleLightAt()` both need it.
2. **Bake static vertices.** `BakeLighting()` walks the graph and, per vertex per light, accumulates `ambient + lambert × attenuation × color`, firing a shadow ray (`rayOccluded()`, Möller–Trumbore, two-sided, first-hit) and skipping any light that's blocked. The result is clamped and written into vertex floats 16–18.
3. **Build the light grid.** The occluder triangles are reduced to a world AABB (`minX`…`maxZ`, snapped outward to integers), and `sampleLightAt()` is evaluated at every integer point inside it into `lightGrid`. Storage order is x outer, z inner, so a cell lives at `(gx * ny + gy) * nz + gz`.

Both bake passes take `parentWorld` **explicitly** rather than reading `world`, because the bake runs before the first `Draw()`/`Compose()` — only roots have a valid `world` at that point and every child's is still identity.

> **Gotcha:** in the `Mesh` overrides, the chain to `Object::CollectOccluders` / `Object::BakeLighting` passes **`parentWorld`, not the composed `world`**. The base recomposes this node's transform itself, so passing `world` applies the mesh's transform twice to every descendant — silent, and geometrically plausible enough to miss.

Because the static result is folded into the vertex data, **a baked mesh must never move afterwards.** The bake is naive `O(verts × lights × tris)` plus `O(cells × lights × tris)` for the grid; it prints its triangle count and elapsed time to stderr. A spatial grid over the occluders is the fix if it ever gets slow.

`SHADOW_BIAS` lifts each ray off the surface it starts on — without it every vertex re-hits its own triangles and the scene self-shadows into speckle. It's tuned for a metre-scale scene.

**Dynamic meshes** take one light value for the whole mesh, looked up per draw from the light grid by `gridLightAt()` at the mesh's world origin (column 3 of `world`, no decompose needed). `gridLightAt()` trilinearly blends the eight surrounding cells so a mover crossing a cell boundary fades instead of popping, clamps out-of-bounds points to the edge cell in *grid space* (before splitting into index and fraction, so weights agree with the clamped indices), and returns fullbright if the grid is empty — no bake means visible, not black.

The remaining asymmetry is that a mover gets no lambert term, so it shades uniformly and reads flatter than static geometry beside it. Shadowing is *not* on that list anymore: because `sampleLightAt()` shadow-tests against the persistent `occluders` list, the grid carries shadow information and a mover passing behind geometry does darken.

**The editor renders everything fullbright** (`LightMode` 0 with the default 1,1,1 baked color, since it never calls `bakeSceneLighting()`), except light gizmos, which self-illuminate their own color via `LightMode` 1 / `ObjectLight` so they're visible as colored markers regardless of scene lighting.

## Rendering specifics

One shader program with inline GLSL (string literals in each `graphics.cpp`) per copy. The vertex shader does linear-blend skinning; verts with zero total weight take a pass-through branch, so unskinned meshes are unaffected by stale palette data. The fragment shader does a Doom-style alpha-cutout (`discard` when `a < 0.5`) and, in a separate `TextMode`, renders font-atlas glyph quads. Shaders compile at *runtime*, so GLSL typos survive `mingw32-make` and only surface as a compile log on launch. `MAX_BONES` is declared in the C++ header and the shader source and must be kept in sync.

**Screen-space UI** (`UIElement` for a single textured quad, `UIText`/`Font` for baked-bitmap-font text) is drawn between `beginUI()`/`endUI()`, after the 3D pass. `bakeFont()` rasterizes a TTF into a fixed ASCII 32–126 atlas (`Glyph[96]`); `layoutText()` rebuilds a `UIText`'s vertex buffer from its `.text` every frame it changes (both apps rebuild it every frame, so string changes are effectively free to author).

## Skinning & animation

Skinned meshes carry a `Skeleton` (inverse-bind matrices, parent indices, per-bone `parentWorld` seed) and a `std::vector<Animation>` — one `Animation` per FBX anim stack, each a set of per-bone `BoneTrack`s (baked pos/rot/scale keyframes). `Mesh::currentAnim` selects the active clip and `Mesh::animTime` tracks playback. `computePose()` samples the current clip at `animTime`, blends the two nearest frames (lerp pos/scale, slerp rot), walks the bone hierarchy to world space, multiplies by the inverse bind, and uploads the resulting palette to `boneMatrices[]`.

**Selecting a clip:** call `Mesh::SetAnimation(int index)` or `Mesh::SetAnimation(const std::string& name)` (matches the FBX anim-stack name; returns `false` if no clip matches). Both reset `animTime` so the new clip starts from frame 0. Meshes default to `currentAnim = 0` (game) / `-1` (editor, which never plays clips by default). Switching is instant — there is no cross-fade blending between clips yet.

## Raycasting (used by editor picking; available to both)

`raycast(origin, dir, maxDist)` walks `parents`, testing every mesh's *current world-space* triangles (via each node's composed `world` — must run after this frame's `Compose()`), and returns the nearest hit `Object*` or `nullptr`. Skinned meshes are tested in bind pose, not the animated pose, since the CPU never sees the shader's skinning. This is how the editor's left-click selection works; the game doesn't currently call it.

## FBX loading (ufbx) gotchas

- Source files include **`ufbx.h`**, never `ufbx.c`. `ufbx.c` is compiled exactly once via the Makefile `COMMON_SRCS`. Including `ufbx.c` in a `.cpp` double-defines every symbol → multiple-definition link errors.
- `loadFBX()` iterates scene **nodes** and bakes each `node->geometry_to_world` into the vertex positions. That matrix carries ufbx's unit (cm→m) and axis (Z-up→Y-up) conversion set via the load opts; reading `mesh->vertex_position` raw would skip it and yield ~100× oversized, wrongly-oriented geometry.
- Some exports load fine but contain no mesh nodes (`0 verts`). That's a bad export, not a loader bug — try re-exporting or a different file.
- **Skinning:** bone indices are collected from the mesh's first `skin_deformer`; each cluster maps to a global bone slot. Inverse-bind is stored as `geometry_to_bone * inverse(geometry_to_world)` so it composes correctly with the baked-in geometry transform. Only the top 4 weights per vertex are kept and renormalized.
- **Animation:** `loadFBX()` bakes **every** `scene->anim_stacks` entry into its own `Animation` (all sharing the same bone list), sampling each bone's local transform at 30 fps across the stack's time range via `ufbx_evaluate_transform`. Clip names come straight from the anim-stack names — check the `baked clip[i] '<name>'` stderr lines for the exact strings to pass to `SetAnimation`. Mixamo often names the stack `"mixamo.com"`, so index-based selection can be more practical for single-take exports.

## Remember

`ufbx_transform_direction` applies the full matrix, which is only correct for normals under rotation/uniform scale — non-uniform scale needs the inverse-transpose. `loadFBX` still uses it, so FBX normals are wrong under non-uniform scale. The game's `Mesh::BakeLighting()` sidesteps this on its own side by using the inverse-transpose of the world matrix, but that can't repair a normal that was already mangled at load time.
