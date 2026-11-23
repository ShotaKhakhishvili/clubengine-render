# Scene file reference (version 1, renderer 0.3)

This is the core: structure, objects, orientation, facts and limits, gotchas. Topic files next to it hold the rest. **Read only the ones the scene needs:**

| file | read it when the scene has |
|---|---|
| `tracks.md` | any animation: keys, easing, repeat/period, spin, wave, orbit, follow, path, bounce, walk |
| `emitters.md` | particles (snow, petals, sparks, rain) or many identical scattered copies (grass, rocks, trees) |
| `materials-lighting.md` | named materials, emissive, unlit, point/spot lights, lighting setups |
| `camera.md` | camera moves or framing beyond a fixed `lookAt` |
| `gltf.md` | `.glb`/`.gltf` models |
| `python-lib.md` | a `.py` scene with `cerender_lib` |
| `cli.md` | launcher details: render modes, output paths, ffmpeg, Linux setup |

One JSON file describes everything. Comments (`//`) are allowed. Unknown keys are errors, with a "did you mean" hint. Big or procedural scenes can be a Python script that prints this JSON instead (python-lib.md).

```jsonc
{
  "version": 1,
  "output":      { ... },   // size, fps, length
  "environment": { ... },   // sky, ground, ambient light, fog
  "materials":   { ... },   // named materials, reused by objects (materials-lighting.md)
  "objects":     [ ... ],   // meshes, lights and exactly one camera
  "animations":  [ ... ]    // tracks that change object properties over time (tracks.md)
}
```

## Conventions

- **Z is up**, X is forward, Y is right (left-handed, like Unreal). The ground is the XY plane at z = 0.
- **Units are meters.** Built-in meshes are 1 unit across and centered on their origin, so a cube resting on the ground has `"position": [x, y, 0.5]`, and one scaled to height `h` sits at z = `h / 2`.
- **Rotation** is `[pitch, yaw, roll]` in degrees. Yaw turns around Z (up), pitch tips the nose up/down, roll banks.
- **Colors** are CSS-like sRGB: `"#ff8a65"`, `"#f86"` or `[1.0, 0.54, 0.4]`.
- **Time** is in seconds.

### Orientation cheat sheet (checked by rendering)

- **Characters face +X.** Their right side is +Y, their left −Y.
- **Yaw** (`rotation.y`): +90 turns something that faces +X to face +Y.
- **A limb hanging down from a joint** (joint group at the shoulder/hip, parts below it along −Z):
  - `pitch` (`rotation.x`) **+** swings it **forward** (+X), **−** swings it back. (Objects pointing *up* lean the other way: see Facts & limits, pitch sign.) Knees bend with −pitch, a kick is +pitch on the hip, reaching forward is +pitch on the shoulder (+90 = straight ahead).
  - `roll` (`rotation.z`) **−** swings it toward **+Y**, **+** toward −Y. So raise the **right** arm (+Y side) out sideways with **−roll** (−150 is a wave), the **left** arm with **+roll**.
- **Which screen side:** screen right is the camera's facing direction turned +90 yaw.
  - Camera in front of a character (at +X looking −X): the character's right (+Y) is on screen **left**, like a mirror.
  - Camera behind it (at −X looking +X): +Y on screen **right**.
  - Side camera at −Y looking +Y: +X is on screen **left**; at +Y looking −Y: +X on screen **right**.

## output

| key | default | notes |
|---|---|---|
| `width`, `height` | 1080, 1080 | pixels, 16–4096 |
| `fps` | 30 | 1–120 |
| `duration` | 0 | seconds; 0 renders one still image |

Common sizes: 1080×1350 (Instagram/LinkedIn portrait), 1080×1080 (square), 1080×1920 (Reels/Stories), 1920×1080 (landscape).

## environment

| key | default | notes |
|---|---|---|
| `sky` | soft blue → warm horizon | `["#top", "#horizon", "#bottom"]` gradient, or one `"#color"` for a flat studio backdrop |
| `ground` | light warm grey at z = 0 | `"#color"`, `{ "color": "#...", "height": 0 }`, or `false` for no ground (objects float in the sky) |
| `ambient` | 0.4 | soft fill light from the sky and ground; 0.2 moody, 0.6 bright and flat |
| `fog` | `{ "start": 25, "end": 90 }` | fades distant things into the horizon color; `false` to disable |
| `exposure` | 1.0 | overall brightness multiplier |

The ground is endless (it fades into the horizon). Every mesh casts a **shadow on the ground** from the first directional light, in its real shape, plus a faint soft darkening right under anything touching the ground. Shadows only fall on the ground: objects don't shadow each other, and point/spot lights cast none. Lower the sun (smaller `z` in its position) for long dramatic shadows, raise it for short ones.

## materials

Named materials live in `"materials"` and are used by name: `"material": "clay"`, or inline: `"material": { "color": "#ff8a65", "roughness": 0.8 }`. Material keys are `color`, `roughness`, `metallic`, `emissive`, `unlit` (details in materials-lighting.md). Only `color` can also go on the object itself.

## objects

Every object has an `id` and at most one of `mesh`, `light` or `camera` (a mesh object with an `emitter` block becomes many instances, see emitters.md). Shared keys:

| key | default | notes |
|---|---|---|
| `id` | required | unique name, used by `parent`, `lookAt` and animations |
| `position` | `[0, 0, 0]` | relative to the parent, if there is one |
| `rotation` | `[0, 0, 0]` | `[pitch, yaw, roll]` degrees, relative to the parent |
| `scale` | 1 | number or `[x, y, z]` |
| `parent` | none | id of another object; this object then moves, turns and scales with it |
| `lookAt` | none | `[x, y, z]` or an object id; turns the object to face it every frame (cameras, lights, heads) |
| `visible` | true | |

### Groups (pivots)

An object with **none** of `mesh`, `light`, `camera` is an invisible group: a pivot to parent other objects to. Use groups as joints (shoulder, hip, neck), as a character's root, or to orbit/spin several things together. Don't fake one with a tiny sphere.

```json
{ "id": "shoulderR", "parent": "bot", "position": [0, 0.34, 1.1] },
{ "id": "armR", "parent": "shoulderR", "mesh": "cylinder", "position": [0, 0, -0.18], "scale": [0.1, 0.1, 0.34] }
```

Groups take `position`, `rotation`, `scale`, `parent`, `lookAt`, `visible` (hiding a group doesn't hide its children; hide the children).

### Mesh objects

| key | notes |
|---|---|
| `mesh` | a built-in shape (table below), or a path to a `.glb`/`.gltf` file relative to the scene file |
| `material` | name from `materials`, or an inline material object |
| `color` | shortcut: the material (or the default one) in this color |
| `shadow` | casts a shadow on the ground, default true (false for planes) |
| `pivot` | where the object's origin sits: `"center"` (default for built-ins), `"bottom"` (center of the base: scale and squash keep it on the floor, `position` z = 0 rests on the ground), `"origin"` (as modeled; default for glTF and lathe) |
| `flat`, `detail` | faceted low-poly look and resolution, see below |

Built-in shapes, generated at load time. All fit a 1 × 1 × 1 box centered on the origin (scale them to size), except `lathe`:

| mesh | notes | options |
|---|---|---|
| `cube`, `plane` | a `plane` lies flat in XY, rotate it to stand it up | |
| `sphere` | | `detail` 0–5 subdivisions (makes an icosphere; 1 is low-poly, 3 smooth), `flat` |
| `cylinder`, `cone` | stand upright along Z | `detail` 3–128 sides, `flat` |
| `torus`, `ring` | lies flat in XY, 1 across; ring is thin (0.12), torus thick (0.35) | `thickness` tube diameter 0.01–0.9, `detail` 3–128, `flat` |
| `roundbox` | a cube with rounded edges | `bevel` edge radius 0–0.5 (default 0.15), `detail` 1–16 steps per edge, `flat` |
| `gem` | brilliant cut, always faceted: table on top, point at the bottom | `detail` 3–64 facets (default 8) |
| `rock` | lumpy stone, faceted by default | `seed` (any whole number, a different stone each), `detail` 0–4, `flat: false` for smooth |
| `lathe` | a profile revolved around Z, in its own coordinates (not unit-sized) | `profile` (required): `[[radius, z], ...]` from bottom to top along the outside; start and end at radius 0 to close it; corners sharper than ~50° stay sharp. `detail` sides (default 48), `flat` |

`"flat": true` gives every triangle its own normal: the faceted low-poly look. Use it with a low `detail` (sphere 1, cylinder 6–10).

```json
{ "id": "vase", "mesh": "lathe", "color": "#b48cff", "profile": [[0, 0], [0.3, 0], [0.4, 0.25], [0.22, 0.7], [0.3, 0.95], [0, 0.95]] },
{ "id": "boulder", "mesh": "rock", "seed": 7, "pivot": "bottom", "scale": [0.8, 0.7, 0.5] },
{ "id": "planet", "mesh": "sphere", "flat": true, "detail": 1, "color": "#7be0b5" }
```

`pivot` works in the mesh's own space, before rotation: a pivot-`bottom` object that you rotate turns around its unrotated base.


### Other object kinds

- **glTF models:** `"mesh": "models/chair.glb"`, see gltf.md (axes, scale, materials).
- **Lights:** `{ "id": "sun", "light": { "type": "directional", "intensity": 1.1 }, "position": [5, -6, 7], "lookAt": [0, 0, 0] }`. Types `directional`, `point`, `spot`; keys and defaults in materials-lighting.md.
- **Camera:** exactly one object with `"camera": {}`, aimed with `lookAt`. Fixed 90° vertical field of view; see camera.md.
- **Emitters:** a mesh object with an `"emitter": { ... }` block becomes many instances; see emitters.md.

## animations

`"animations"` is a list of tracks; each sets one property of one object over time (`"target": "ball.position.z"`). Everything about them is in tracks.md.

## Facts & limits

Checked against the engine and project source and with test renders (renderer 0.3).

- **Lights: no limit.** Point, spot and directional lights all go into growable GPU buffers; a test with 200 lights (100 point, 100 spot) lit all 200. Each one costs time on every pixel: that frame took 4.4 s instead of 0.9 s on the software renderer. Lights with `intensity` 0 or `visible: false` are skipped. Only the first directional light casts ground shadows.
- **Material keys vs object keys.** `roughness`, `metallic`, `emissive`, `unlit` exist only in a material (named, or inline `"material": { ... }`); on an object they're "unknown key" errors. Object keys: `id`, `mesh`, `light`, `camera`, `material`, `color`, `position`, `rotation`, `scale`, `parent`, `lookAt`, `shadow`, `visible`, `pivot`, `detail`, `flat`, `thickness`, `bevel`, `profile`, `seed`, `emitter`. Light keys go inside `light`.
- **Emissive color is fixed per material.** A numeric `emissive` is the material's own `color` × the number, computed when the scene loads. An object `color`, an emitter color list or a `color` track change the base color but not the glow (a blue glowing material with `"color": "#ff2000"` on the object still glows blue). Use one material per glow color, or `"emissive": "#color"`.
- **Emissive brightness.** Emissive is added to the lighting, then tonemapped per channel; a channel tops out near linear 1.15. In the dark, `emissive: 1` shows about the authored color. From about 1.2 the strongest channel is maxed and the weaker ones keep rising, so hues drift: orange turns yellow, pink turns lilac, pastels reach white by 1.5–2. Colors with a zero channel (pure cyan, pure green) never reach white. In a lit scene the surface already shows its color at 0 and the drift starts around 0.5. Saturated glow: 0.8–1.2 in the dark; when lit keep it ≤ 0.5 or darken the base color.
- **glTF emissive:** `emissiveFactor` × `KHR_materials_emissive_strength` is supported, with the same clipping.
- **Fog affects everything**, emissive and unlit materials included (an emissive and an unlit cube 60 m away came out about half horizon color with the default fog 25–90). For distant neon, push `fog.start` beyond it or set `"fog": false`.
- **glTF axes and scale:** engine (x, y, z) = (glTF z, −glTF x, glTF y). glTF +Y (up) → +Z, +Z (front) → +X, +X → −Y. Scale 1 (meters), origin as modeled, no recentering. A model facing +Z faces +X here.
- **Emitter meshes:** any mesh works: every built-in shape (`lathe`, `rock`, `gem`, ...) and `.glb`/`.gltf` (tested with each).
- **Emitters honor `parent` and tracks.** A moving parent carries all instances; tracks on the emitter's `position`, `rotation`, `scale` and `visible` apply to all of them; `color` tracks are rejected. A hidden parent doesn't hide it (visibility is never inherited).
- **Pitch sign:** +pitch turns +X up toward +Z and +Z toward −X. Ramp along +X: +pitch raises its +X end. Upright post or chain on its base: −pitch leans its top toward +X. Hanging limb: +pitch swings it forward (+X).
- **`lookAt` and `visible` can be keyed.** `lookAt` keys are points and replace an object target. `visible` keys (`[t, true]`) are eased like 0/1 and flip at 0.5, so with the default ease the switch lands halfway between keys: put `"step"` on the key before the switch to switch exactly at the next key (tracks.md).
- **Frames:** a clip has duration × fps frames, numbered 0 to duration × fps − 1 (15 s at 30 fps: 0–449). Frame n is at n / fps seconds.
- **`.py` scenes:** always render them through `cerender.py render scene.py`. `python scene.py` on its own fails with `ModuleNotFoundError: cerender_lib` unless `PYTHONPATH` points at the plugin's `scripts/` folder.
- **No phase offset option for `repeat`:** start the keys earlier. A track runs from its first key, so `[[-0.5, a], [0, b], [0.5, a]]` with `"repeat": true` is half a 1 s cycle ahead of the same keys at 0–1. `wave` has `phase`.
- **Renderer limits** (planned in the engine) and the current best workaround:
  - No transparency or alpha blending (glTF alpha-mask cutouts work): fade out by keying `scale` to 0 or `color` toward the background, then `visible: false`.
  - No bloom: emissive about 1 for the core, plus a point light in the same color so nearby surfaces pick up the glow.
  - Emitter instances keep their size and color for life: layer 2–3 emitters (small and bright, larger and dim) and key each emitter's `scale` or `visible`.
  - No bursts (emitters run continuously from t = 0): for an impact, show a small static emitter (`rotation: "random"`) with `visible` keys at the hit and key its `scale` from about 0.1 to 1 over 0.2–0.4 s with `outCubic`; the cloud expands around the emitter's origin (center the area on it) and instances grow with it.

## Gotchas

- **Flat meshes lying on the ground hide ground shadows.** Shadows are drawn on the ground itself, so a road, path or rug (a `plane` or thin box at z ≈ 0) covers them. Keep such pieces small, or accept no shadows on them.
- **Keep surfaces at least 3 cm apart.** Faces closer than that (a decal on a box, a belt on a body) can flicker or show through, especially far from the camera. Make the outer part a bit bigger instead of nearly touching.
- **Non-uniform parent scale doesn't shear rotated children.** Scaling a parent `[1, 3, 1]` scales a rotated child along the child's own axes, not the parent's: build stretched parts by scaling the part, not its group.
- **The ground renders lighter than its hex** (sun plus ambient light it). For a seamless backdrop make it a bit darker than the sky's bottom color; `examples/studio.json` has a matched set.
- **Point lights leave a bright patch on the ground** near them. Keep them high, small in `radius`, or away from the floor.
- **Strong emissive washes colors out** toward white (cyan, pink). Keep `emissive` ≤ 1.5 for saturated colors; Facts & limits has the measured values.
- The field of view is a wide 90°: things look smaller than you expect. Get the camera close.

## Errors

The renderer checks the whole file before drawing and prints every problem with its JSON path, for example:

```
error: 2 problem(s) in scene.json
  - objects[1]: unknown key "postion" (did you mean "position"?)
  - animations[0].target: no object with id "bal"
```
