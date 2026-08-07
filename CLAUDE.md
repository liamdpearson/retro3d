# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A small, from-scratch OpenGL 3.3 (core profile) 3D game engine written as a learning project, in the style of Half-Life 1 (baked lighting, alpha-cutout textures, capsule-vs-triangle collision). Windows + MinGW `g++`, driven from Git Bash. No engine framework, no package manager — everything is hand-written or vendored.

Two executables share one engine *design* but only partly share code: `game.exe` (plays a scene) and `editor.exe` (edits one — moves objects, tweaks lights, exports `scene.json`). See "The game/editor split" below before assuming a fix in one applies to the other.

## Build & run

Run from **Git Bash** at the repo root (the Makefile also works from PowerShell/cmd — see its "shell portability" note):

- `mingw32-make` — build both `game.exe` and `editor.exe`
- `mingw32-make game` / `mingw32-make editor` — build one target only
- `mingw32-make run` — build + launch `game.exe`
- `mingw32-make run-editor` — build + launch `editor.exe`
- `mingw32-make clean` — remove both exes and all object files
- add `-j8` to build in parallel; each source now compiles to its own object, so it is close to a linear speedup

There is **no test suite and no linter**. Correctness is checked by running the app and looking (or listening). The build passes `-Wall -Wextra`; unused-parameter warnings in GLFW callbacks and missing-field-initializer warnings on `UIText` aggregate init are known and expected. Warnings are switched off for the vendored objects only.

Objects land in `build/`, mirroring the source path (`build/src/game/game.cpp.o`), so two sources with the same basename can't collide. `-MMD -MP` generates header dependencies — without it, editing `graphics.h` would leave every object that includes it stale.

Asset and scene paths are **relative to the working directory**, so always launch from the repo root (e.g. `assets/gun/gun.obj`, `scene.json`).

Diagnostic `fprintf(stderr, ...)` output is **block-buffered when redirected to a file** on this MinGW setup and won't appear until the process exits — the render loop never exits, so run in a real terminal to see it live. This matters more than it sounds: `initAudio()`, `makeEntity()`, and the lighting bake all report through stderr.

## Layout

- `src/engine/graphics/` — window/GL init, shaders, texture/model loaders, skinning/animation, text/UI, all draw logic, and the core scene-graph types. Also **defines** `occluders`, `colliders`, and `lightGrid`.
- `src/engine/lighting/` — the lighting bake: `sampleLightAt()`, `bakeSceneLighting()`, and the `CollectOccluders()`/`BakeLighting()` node walks.
- `src/engine/collisions/` — `raycast()`, the `Raycast()`/`CollectColliders()` node walks, and capsule-vs-triangle resolution for `Player` and `Entity`.
- `src/engine/audio/` — miniaudio wrapper: voice pool, handles, 2D and spatial playback, the listener, and the `AudioSource` scene node.
- `src/game/game.cpp` / `game.h` — the game *application*: scene import, the render loop, FPS input, player/entity movement. **Defines** `camera`, `player`, `parents`, `lights`, `uiElements`.
- `src/editor/editor.cpp` / `editor.h` — the editor *application*: scene import/export, gizmo-based editing, hierarchy + inspector text UI.
- `src/editor/editor_graphics.cpp` / `editor_graphics.h` — the editor's own private copy of the graphics engine.
- `third_party/` — vendored deps, no package manager: `glad` (GL loader, `glad.c` compiled in), `glfw` (static `libglfw3.a`), `glm` (header-only), `stb` (image + truetype), `ufbx` (FBX loading, `ufbx.c` compiled in), `miniaudio` (audio, `miniaudio.c` compiled in), `nlohmann/json.hpp` (scene serialization, header-only).
- `assets/engine_assets/` — gizmo meshes the editor draws in place of data-only nodes (`camera/`, `light/`, `pivot/`, `player/`, `sound/`), so otherwise-invisible nodes are visible and clickable. The game never touches this directory.
- `scene.json` — the scene definition both exes read (editor also writes it).
- `README.md` — user-facing scene-format docs. **Currently stale**: it lists only the original five node types and omits `player`, `entity`, and `sound`.

## The game/editor split

`src/engine/*` is shared **only by the game**. The editor links `src/editor/editor_graphics.cpp` instead, which is an independent copy of the graphics module — see the `Makefile`: `GAME_SRCS` lists `ENGINE_SRCS`, `EDITOR_SRCS` does not. A fix to the vertex format, a loader, or the animation code almost certainly needs making **twice**. Grep both `src/engine/` and `src/editor/` before assuming a change is complete.

The editor has **no counterpart at all** to `lighting`, `collisions`, or `audio` — those three modules are game-only. The editor's picking raycast is its own implementation inside `editor_graphics.cpp`.

Why the editor stays separate: it needs things the game doesn't (per-type mesh subclasses for JSON round-tripping, gizmo meshes, scene export) and deliberately skips things the game needs (the lighting bake — the editor always renders fullbright so placement isn't fighting stale baked shadows).

### Known divergences — check these before relying on either side

- **The editor does not know what an `entity` is.** `buildNode()` in `editor.h` handles `light`, `mesh-obj`, `mesh-fbx`, `camera`, `sound`, `pivot`, and `player` — but not `entity`. `scene.json` currently contains an entity node. **Opening the scene in the editor and pressing Ctrl+S will silently drop it**, because import skips the unknown type (printing to stderr, which you won't see if redirected) and export only writes what it imported. Back up `scene.json` before editing it, or teach the editor the type first.
- **Lights are represented differently.** In the game a `"light"` node becomes a data-only `Light` (pos/color/intensity/radius) pushed onto the separate `lights` list — never a graph node, no mesh. In the editor it becomes a `LightMesh : Mesh` carrying the same fields *plus* a visible gizmo, living in `parents` like any other object so it can be selected and dragged.
- **`Object` carries a `type` string in the editor** but not in the game. The editor needs it to round-trip a node back to JSON and to label the inspector and dispatch input; the game never serializes.
- **Mesh subtyping is inverted.** The game has one `Mesh` (with `isStatic`/`collides` on it) plus `Entity : Mesh`. The editor has no flags on `Mesh` and instead subtypes per scene-node kind — `LightMesh`, `CameraMesh`, `ObjMesh`, `FbxMesh`, `SoundMesh` — purely so `objectToJson()` can recover each type's fixed fields on export.
- **`makeFbx()` signatures differ**: the editor's takes an explicit `isStatic` bool, the game's does not (FBX meshes are implicitly dynamic there).
- **Animation APIs differ**: the game's `SetAnimation(index, blendTime, nextAnim)` has a third parameter for queuing a follow-on clip; the editor's `SetAnimation(index, blendTime)` does not, and the editor's `Mesh` has no `nextAnim` field.
- **The editor has no `Player`/`Entity` structs.** A `"player"` scene node becomes a plain gizmo `Mesh` with `type == "player"`.

## Scene format (`scene.json`)

Top level is `{"scene": [...]}`, a list of nodes. Each node has a `"type"` and, except for lights and the player, a 7-float `"transform"` array: `[x, y, z, yaw, pitch, roll, scale]`. Optional `"name"` and `"tag"` on any node. Children nest under `"children"`.

| type | fields | notes |
|------|--------|-------|
| `pivot` | — | invisible grouping / attachment node |
| `mesh-obj` | `obj src`, `tex src`, `isStatic`, `collides` | static-capable OBJ geometry, no animation |
| `mesh-fbx` | `obj src`, `tex src` | FBX geometry, can carry baked animation clips |
| `entity` | `obj src`, `tex src`, `ratiohr` | FBX mesh that also collides; game-only, **editor drops it** |
| `light` | `position`, `color`, `intensity`, `radius` | not a graph node; cannot have children or be a child |
| `camera` | `transform`, `fov` | one per scene; placed, not allocated |
| `player` | `position` (3 floats), `yaw` | the collidable capsule; placed, not allocated |
| `sound` | `src`, `volume`, `minDist`, `maxDist`, `rolloff`, `loop` | spatial emitter, see Audio below |

`camera` and `player` are the two nodes `buildNode()` does not heap-allocate — it points at the globals `camera`/`player` and returns them so they still land in `parents`, which `Compose()` requires.

Both `buildNode()` (import, in `game.h`/`editor.h`) and, editor-side, `objectToJson()` (export, in `editor.h`) must agree on these fields — the export path packs a node's type-specific data by `dynamic_cast`ing to the concrete mesh subtype, since none of it lives on the base `Object`.

`entity`'s `ratiohr` is a radius-to-height *ratio*: `makeEntity()` measures the loaded mesh's height, scales it by the transform's scale, and multiplies by `ratiohr` to get the collision radius.

## Architecture

**Global engine state, not objects.** The window, shader program, uniform locations (`modelLoc`/`viewLoc`/`projectionLoc`/`boneMatricesLoc`/`lightModeLoc`/`objectLightLoc`/`textModeLoc`), the occluder/collider lists, and the light grid are plain globals. This is deliberate; don't refactor it into classes without reason.

**Where globals actually live is inconsistent, and one arrangement is fragile.** `graphics.h` declares all of them `extern`, but:

- `occluders`, `colliders`, `lightGrid`, the scene-bounds floats, and the GL handles are defined in `graphics.cpp` — fine.
- `lightAmbient` and `sensitivity` are defined in `game.cpp` — fine.
- **`camera`, `player`, `parents`, `lights`, and `uiElements` are defined in `src/game/game.h`** — a *header*. That links only because exactly one translation unit (`game.cpp`) includes it. Including `game.h` from a second `.cpp` gives duplicate-symbol link errors. If the game ever grows a second source file, move these definitions into a `.cpp` first.

Note `yaw`, `pitch`, `lastX`, `lastY`, and `firstMouse` are still globals, but only the mouse-tracking three are live — `yaw`/`pitch` are vestigial from before the camera became a scene node and drive nothing.

**Fixed vertex format: 19 interleaved floats per vertex** (`VERTEX_FLOATS`), in this order:

| offset | count | attribute |
|--------|-------|-----------|
| 0  | 3 | position `x, y, z` |
| 3  | 2 | UV `u, v` |
| 5  | 3 | normal `nx, ny, nz` |
| 8  | 4 | bone indices (stored as floats, cast to int in the shader) |
| 12 | 4 | bone weights |
| 16 | 3 | baked light color `r, g, b` |

This is load-bearing. `uploadObject()` hardcodes stride `VERTEX_FLOATS * sizeof(float)` and sets up attribute slots 0–5 to match. Every mesh source (`loadOBJ`, `loadFBX`) must emit exactly this layout — unskinned meshes just leave bone indices/weights zeroed. Changing the format means editing this table, `uploadObject()`'s attribute pointers, **and** the vertex shader in lockstep — in both copies.

Baked light **defaults to `1.0, 1.0, 1.0`, not zero** — it multiplies the texture, so an unbaked mesh (or anything drawn by the editor, which never bakes) renders fullbright and visible rather than black and invisible.

Bumping `VERTEX_FLOATS` without emitting the matching floats in a loader is a *silent* failure: nothing fails to compile, the stride just walks out of step and that one mesh renders as garbage.

## The scene graph

**`Object` is a bare scene node** — a name, a tag, a local `Transform`, a cached `world` mat4, a `parent` pointer, and `children`. It carries no geometry, so a plain `Object` is a pivot / attachment point / group whose transform still composes into everything beneath it.

**Every walk over the graph is a virtual pair.** The base `Object` method does the recursion and nothing else; the derived type overrides it to do its own work and then chains to the base. That way a mesh-less node is never a special case at the call site. The walks are `Upload()`, `Compose()`, `Draw()`, `Raycast()`, `CollectOccluders()`, `BakeLighting()`, and `CollectColliders()`.

The node types, all game-side:

- **`Mesh : Object`** — CPU mesh (`vertices`/`indices`), GL handles (`VAO/VBO/EBO/texture`, `indexCount`), `isStatic`/`collides` flags, and for skinned FBX a `Skeleton` plus baked `animations`. Its destructor frees the GL handles, which is why `~Object()` is virtual.
- **`Player : Object`** — an invisible capsule (`velocity`, `radius`, `height`, `grounded`). No geometry; the camera hangs off it as a child.
- **`Entity : Mesh`** — a mesh that also collides, with its own `velocity`/`radius`/`height`. Same capsule resolution as the player but a cheaper one — see Collisions.
- **`Camera : Object`** — `FOV` plus derived `pos`/`front`/`up`.
- **`AudioSource : Object`** — a spatial emitter; see Audio.

**Transforms are kept as matrices to dodge gimbal lock.** `Transform` is position + `yaw` + `pitch` + `roll` + uniform `scale`, and `Transform::matrix()` builds `T * R(yaw) * R(-pitch) * R(roll) * S`. Composed world transforms are stored as `glm::mat4` and never decomposed back to Euler angles, which preserves roll from nested rotations. Keep it that way. `Transform::scale` defaults to 1, never 0 — a zero there silently collapses every descendant to a point.

**`Camera::Compose()` reads `pos`/`front`/`up` straight out of the composed `world`** (via `mat3(world)` to drop translation, then normalize — a scaled parent leaks into the basis) rather than rebuilding them from yaw/pitch, so the camera inherits parent rotation including roll. Local axes follow the GL convention: forward is -Z, up is +Y. **The camera must be reachable from `parents`** or `Compose()` never runs on it and the view silently freezes at whatever the constructor set. (In the editor, `main()` pushes `&camera` into `parents` explicitly after `importScene()`, since the scene's own `"camera"` node is a separate gizmo object, not the fly-camera.)

**Compose is a separate pass from Draw, and the order matters.** `Object::Compose()` sets `child->world = this->world * child->transform.matrix()` down the graph. It is deliberately *not* fused into `Draw()`: the camera derives `pos`/`front` during Compose, and `clearBG()` needs those to build the view matrix before the first mesh is drawn. Fusing them made the view matrix trail the scene by one frame.

**`findFirst(tag, parents)`** searches the graph by tag and returns the first match, which is how `game.cpp` gets typed pointers to authored nodes (`dynamic_cast` to `Mesh*`/`Entity*`/`AudioSource*` after).

## Per-frame flow (game)

1. `glfwPollEvents()` **first**, so the frame acts on this frame's input.
2. Update `deltaTime`.
3. Act on edge-triggered input (`mouseButtonPressed`/`keyPressed`), then **clear the pressed queues** — see Input below.
4. `movementLogic()` — accumulate WASD acceleration, gravity, and damping into `player.velocity`.
5. Integrate the entity and the player, each **substepped**, calling `resolveEntityCollision()` / `resolvePlayerCollision()` after every substep.
6. For each root: `obj->world = obj->transform.matrix()` then `obj->Compose()`.
7. `updateAudio(camera.pos, camera.front, camera.up)` — must be after Compose.
8. `clearBG()` — sets viewport/scissor, builds `projection` and `view` from the camera, uploads them once.
9. For each root: `obj->Draw()`. Then `beginUI()` / draw `UIElement`s and `UIText`s / `endUI()`.

`drawObj()` advances `animTime`, uploads the per-mesh `model` matrix, sets `LightMode` (and `ObjectLight` for movers), uploads the bone palette for skinned meshes, then issues `glDrawElements`.

**Substepping is not optional.** The move is split into steps no longer than half the capsule radius, because collision resolution only fixes an overlap that still *exists* when it runs — a step big enough to clear a surface entirely lands past it with nothing left to detect. That is the classic way to fall through a floor after building up speed. `grounded` is accumulated across substeps (`groundedAny`) because `resolvePlayerCollision()` clears it on entry, and the last substep usually can't re-detect ground the previous one already pushed clear of.

### Input

`game.cpp` keeps four vectors — `keys_pressed`, `keys_released`, `mouse_buttons_pressed`, `mouse_buttons_released` — filled by the GLFW callbacks and queried with `keyPressed()` / `keyReleased()` / `mouseButtonPressed()` / `mouseButtonReleased()`. These are **edge-triggered**, and the loop clears the two `_pressed` vectors each frame after acting on them. Anything reading them must run before that clear. Held state uses `glfwGetKey()` directly instead (as `movementLogic()` does).

FPS-style camera, no edit/game mode toggle, no pause key. `mouseMoveCallback` puts **yaw on the player** and **pitch on the camera** — because the player is the thing that turns and the camera is its child — and clamps pitch to ±89°. `Escape` quits.

### Editor input

Same fly camera (yaw and pitch both on the camera there), plus:

- **Left-click** raycasts and sets `curObject` to the hit (or `nullptr`).
- **Number keys** point `curElement` (a `float*`) at one field of the selection and set `editMultiplier` for its sensitivity: `1`/`2`/`3` = position x/y/z on anything; `4`/`5`/`6`/`7` = yaw/pitch/roll/scale normally, but r/g/b/intensity on a light, and yaw only on the player. `8` is per-type — light radius, camera FOV, or toggling `isStatic` on a mesh. `9` toggles `collides` on an OBJ mesh. `←`/`→` step the current animation clip on an FBX.
- **Holding right mouse** while a field is armed feeds mouse delta into `*curElement` instead of turning the camera.
- **`Ctrl+S`** exports to `scene.json`; **`Ctrl+C`/`Ctrl+V`** copy/paste a node; **`Delete`** removes one.
- The inspector (`curObjectLabel`) and hierarchy (`hierarchyLabel`, built by `buildHierarchyString()`) are redrawn as screen-space `UIText` every frame.

## Lighting (game only — the editor never bakes)

Baked, Half-Life 1 style — two paths chosen by `Mesh::isStatic`. A `Light` is a point light: `pos`, `color`, `intensity`, `radius`. There is no realtime per-pixel lighting; the fragment shader multiplies the texture by a light value and the two paths differ only in where that value comes from (`uniform int LightMode`: 0 = baked per-vertex, 1 = per-object).

**The attenuation curve is duplicated between `sampleLightAt()` and `Mesh::BakeLighting()` and must stay identical**, or movers and the baked floor beneath them will disagree about how bright the room is. Both use `intensity / (1 + d² / radius)` — the `1 +` keeps a light sitting exactly on a vertex from dividing by zero. Not physical, but stable and easy to tune.

`bakeSceneLighting()` runs after the scene graph is assembled and the lights are placed but **before** `Upload()`. Three stages:

1. **Collect occluders.** `CollectOccluders()` flattens every static triangle into the world-space global `occluders`. Dynamic meshes are excluded: they move, and a shadow baked from them wouldn't. The list stays alive past the bake — the grid stage, `sampleLightAt()`, and audio occlusion all read it.
2. **Bake static vertices.** `BakeLighting()` walks the graph and, per vertex per light, accumulates `ambient + lambert × attenuation × color`, firing a shadow ray (`rayOccluded()`, Möller–Trumbore, two-sided, first-hit) and skipping any blocked light. The result is clamped into vertex floats 16–18.
3. **Build the light grid.** The occluder triangles are reduced to a world AABB (`minX`…`maxZ`, snapped outward to integers), and `sampleLightAt()` is evaluated at every integer point inside it into `lightGrid`. Storage order is x outer, z inner: `(gx * ny + gy) * nz + gz`.

Both bake passes take `parentWorld` **explicitly** rather than reading `world`, because the bake runs before the first `Compose()` — only roots have a valid `world` at that point.

> **Gotcha:** in the `Mesh` overrides, the chain to `Object::CollectOccluders` / `Object::BakeLighting` passes **`parentWorld`, not the composed `world`**. The base recomposes this node's transform itself, so passing `world` applies the mesh's transform twice to every descendant — silent, and geometrically plausible enough to miss. `Mesh::CollectColliders` has the same shape and the same trap.

Because the static result is folded into vertex data, **a baked mesh must never move afterwards.** The bake is naive `O(verts × lights × tris)` plus `O(cells × lights × tris)` for the grid; it prints its triangle count and elapsed time to stderr.

`SHADOW_BIAS` lifts each ray off the surface it starts on — without it every vertex re-hits its own triangles and the scene self-shadows into speckle. Tuned for a metre-scale scene.

**Dynamic meshes** take one light value for the whole mesh, looked up per draw from the light grid by `gridLightAt()` at the mesh's world origin (column 3 of `world`, no decompose). It trilinearly blends the eight surrounding cells so a mover crossing a boundary fades instead of popping, clamps out-of-bounds points to the edge cell in *grid space* (before splitting into index and fraction, so weights agree with the clamped indices), and returns fullbright if the grid is empty.

The remaining asymmetry is that a mover gets no lambert term, so it shades uniformly and reads flatter than static geometry beside it. Shadowing is *not* on that list: `sampleLightAt()` shadow-tests against `occluders`, so the grid carries shadow information and a mover passing behind geometry does darken.

## Collisions (game only)

`collectSceneColliders()` runs once at startup and flattens every triangle from meshes that are **both `isStatic` and `collides`** into the global `colliders`, as `TriAABB` (a `Tri` plus its precomputed `AABB`). Occluders and colliders are deliberately separate lists with separate flags — otherwise you couldn't have static geometry that blocks light but not the player, or vice versa.

`resolvePlayerCollision(Player&)` is capsule-vs-triangle, run up to `MAX_ITERATIONS = 4` times per call because pushing out of one triangle can push you into another:

1. Rebuild the player's `AABB` (expanded by a small margin) per pass and broad-phase against each `TriAABB`.
2. Reduce capsule-vs-triangle to sphere-vs-triangle: intersect the capsule axis with the triangle's plane, clamp that point into the triangle, take the nearest point on the axis to it as the sphere center, then the nearest point on the triangle to that as the contact.
3. If the gap is under `radius`, push out along the separating direction by the penetration depth, and cancel only the component of velocity heading *into* the surface.

**The ground case is special-cased.** If the push direction's `y` exceeds `GROUND_NORMAL_Y` (0.7), the node is only pushed **up** — x and z are left alone — and `grounded` is set. That is what stops the player sliding down slopes shallow enough to walk on.

`resolveEntityCollision(Entity&)` is the same algorithm with `MAX_ITERATIONS = 1` and no ground handling.

`closestPointOnTriangle()` walks the triangle's Voronoi regions, so it is correct whether the nearest feature is the face interior, an edge, or a corner — the distinction a box test can't make, and why it runs second.

## Audio

miniaudio, wrapped so that `ma_*` types never appear outside `audio.cpp`. `audio.h` deliberately breaks the "globals extern in the header" convention: miniaudio.h is ~11.5k lines even with the implementation off, and keeping the backend hidden is what would make swapping `ma_engine` for a hand-written mixer a one-file change. `audio.h` does include `graphics.h`, because `AudioSource` is a scene node — that's the graph, not the backend.

`initAudio()` failure is **not fatal**: a machine with no working device runs the game in silence, and every other function is a safe no-op.

**Voices are a fixed pool of 32**, never grown — allocating in response to gameplay is how you get a frame hitch. Past 32, new sounds are dropped rather than stealing a playing voice.

**Handles, not indices.** `SoundHandle` is `{slot, generation}`; `generation` is bumped in `releaseVoice()`, the single place a slot is freed. `resolve()` is the only place a handle becomes an `ma_sound*`, and it rejects a handle whose generation no longer matches. Without that, a stale handle would silently control whatever sound recycled into its slot. Every mutator no-ops on a dead handle, so callers never have to check first.

`initVoice()` / `startVoice()` are split so a voice can be positioned and configured **while still silent** — a 3D sound positioned after it starts gets one audio callback spatialized against the origin, which clicks audibly.

**`updateAudio(pos, front, up)` is the per-frame tick**: it sweeps finished voices, caches the listener position in the file-static `listener`, and pushes the camera basis at miniaudio. Call it once per frame **after Compose** — the camera basis is only valid then. No axis conversion is needed: miniaudio's listener is right-handed, -Z forward, +Y up, exactly matching `Camera::Compose()`. Pass the camera's own `up`, not a hardcoded world up, so the stereo field rolls when the camera does.

**`AudioSource::Compose()` is what keeps moving sounds accurate.** It pushes `world[3]` at its voice and chains to the base — so a source parented to a mover tracks it with no bookkeeping, the same way a child mesh does. `Play()` reads `world`, which `Compose()` fills in, so a source played before the first Compose starts one frame stale.

**Occlusion** reuses the lighting bake's `occluders` list: `soundBlocked()` fires one ray from emitter to listener and, on a hit, ducks the volume to `0.3 ×`. Guarded by `soundPlaying()` so silent sources don't pay for the ray. Two notes: the cached `listener` is one frame behind (Compose runs before `updateAudio()`), which is fine for a binary test; and the ray is `O(occluders)` per playing source per frame, so add a distance cull before it if the source count grows.

**Attenuation is `ma_attenuation_model_linear`**, not miniaudio's inverse default: `gain = 1 - rolloff × (clamp(d, min, max) - min) / (max - min)`, which actually reaches zero at `maxDistance`. Inverse is `1/x` and never reaches zero — it only stops *falling* at `maxDistance`, leaving every source audible forever at roughly -32 dB. The trade is that linear stays loud further out then dies quickly near the edge, so it sounds less natural but is far more controllable. Under linear, `rolloff` and `maxDistance` are **not independent**: rolloff above 1 hits silence early and leaves a dead ring inside the radius, below 1 never quite gets there. Leave it at 1 unless you want one of those.

Doppler is explicitly switched off (miniaudio defaults it on) — it needs a velocity nothing supplies, and one recovered from per-frame position deltas warbles with the frame time.

## Rendering specifics

One shader program with inline GLSL (string literals in each `graphics.cpp`) per copy. The vertex shader does linear-blend skinning; verts with zero total weight take a pass-through branch, so unskinned meshes are unaffected by stale palette data. The fragment shader does a Doom-style alpha-cutout (`discard` when `a < 0.5`) and, in a separate `TextMode`, renders font-atlas glyph quads. Shaders compile at *runtime*, so GLSL typos survive `mingw32-make` and only surface as a compile log on launch. `MAX_BONES` is declared in the C++ header and the shader source and must be kept in sync.

**Screen-space UI** (`UIElement` for a single textured quad, `UIText`/`Font` for baked-bitmap-font text) is drawn between `beginUI()`/`endUI()`, after the 3D pass. `bakeFont()` rasterizes a TTF into a fixed ASCII 32–126 atlas (`Glyph[96]`); `layoutText()` rebuilds a `UIText`'s vertex buffer from its `.text` every frame it changes (both apps rebuild every frame, so string changes are free to author).

## Skinning & animation

Skinned meshes carry a `Skeleton` (inverse-bind matrices, parent indices, per-bone `parentWorld` seed) and a `std::vector<Animation>` — one per FBX anim stack, each a set of per-bone `BoneTrack`s of baked pos/rot/scale keyframes. `computePose()` samples the current clip at `animTime`, blends the two nearest frames (lerp pos/scale, slerp rot), walks the bone hierarchy to world space, multiplies by the inverse bind, and uploads the palette to `boneMatrices[]`.

**Cross-fading between clips.** `BonePose` keeps a bone's local transform as TRS rather than a matrix precisely so two poses can be blended joint by joint — blending matrices instead shears bones as they cross over. `lastPose` is what `computePose()` actually put on screen last frame; `SetAnimation()` copies it into `blendFrom`, and while `blendDuration > 0` the pose is `blendFrom → new clip` weighted by `blendElapsed / blendDuration`. A mesh that has never been drawn has no pose to leave, so its first clip starts instantly whatever `blendTime` says.

**`nextAnim` queues a follow-on clip** (game only): when the current clip finishes, the animation at `nextAnim` starts and repeats. That is how the gun plays a one-shot fire clip and returns to idle — `gun->SetAnimation(1, 0.03f, 0)`.

**Selecting a clip:** `SetAnimation(int index, float blendTime = 0, int nextAnim = -1)` or the `std::string` overload (matches the FBX anim-stack name, returns `false` if no clip matches). Both reset `animTime`. Meshes default to `currentAnim = -1`.

## Raycasting

`raycast(origin, dir, maxDist)` walks `parents`, testing every mesh's *current world-space* triangles (via each node's composed `world` — must run after this frame's `Compose()`), and returns the nearest hit `Object*` or `nullptr`. Skinned meshes are tested in **bind pose**, not the animated pose, since the CPU never sees the shader's skinning. This is how the editor's left-click selection works; the game doesn't currently call it, though audio occlusion uses the same Möller–Trumbore kernel against `occluders` directly.

## FBX loading (ufbx) gotchas

- Source files include **`ufbx.h`**, never `ufbx.c`. `ufbx.c` is compiled exactly once via the Makefile's `COMMON_SRCS`. Including `ufbx.c` in a `.cpp` double-defines every symbol → multiple-definition link errors. The same applies to `miniaudio.c`.
- `loadFBX()` iterates scene **nodes** and bakes each `node->geometry_to_world` into the vertex positions. That matrix carries ufbx's unit (cm→m) and axis (Z-up→Y-up) conversion set via the load opts; reading `mesh->vertex_position` raw would skip it and yield ~100× oversized, wrongly-oriented geometry.
- Some exports load fine but contain no mesh nodes (`0 verts`). That's a bad export, not a loader bug.
- **Skinning:** bone indices come from the mesh's first `skin_deformer`; each cluster maps to a global bone slot. Inverse-bind is stored as `geometry_to_bone * inverse(geometry_to_world)` so it composes correctly with the baked-in geometry transform. Only the top 4 weights per vertex are kept and renormalized.
- **Animation:** `loadFBX()` bakes **every** `scene->anim_stacks` entry into its own `Animation` (all sharing the same bone list), sampling each bone's local transform at 30 fps across the stack's time range via `ufbx_evaluate_transform`. Clip names come straight from the anim-stack names — check the `baked clip[i] '<name>'` stderr lines for the exact strings. Mixamo often names the stack `"mixamo.com"`, so index-based selection is more practical for single-take exports.

## Remember

`ufbx_transform_direction` applies the full matrix, which is only correct for normals under rotation/uniform scale — non-uniform scale needs the inverse-transpose. `loadFBX` still uses it, so FBX normals are wrong under non-uniform scale. `Mesh::BakeLighting()` sidesteps this on its own side by using the inverse-transpose of the world matrix, but that can't repair a normal already mangled at load time.
