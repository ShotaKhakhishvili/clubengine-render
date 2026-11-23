# Scene file reference (version 1, renderer 0.3)

One JSON file describes everything. Comments (`//`) are allowed. Unknown keys are errors, with a "did you mean" hint. Big or procedural scenes can be a Python script that prints this JSON instead: see [Python scenes](#python-scenes-py).

```jsonc
{
  "version": 1,
  "output":      { ... },   // size, fps, length
  "environment": { ... },   // sky, ground, ambient light, fog
  "materials":   { ... },   // named materials, reused by objects
  "objects":     [ ... ],   // meshes, lights and exactly one camera
  "animations":  [ ... ]    // tracks that change object properties over time
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
  - `pitch` (`rotation.x`) **+** swings it **forward** (+X), **−** swings it back. Knees bend with −pitch, a kick is +pitch on the hip, reaching forward is +pitch on the shoulder (+90 = straight ahead).
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

```json
"materials": {
  "clay":  { "color": "#ff8a65", "roughness": 0.8 },
  "chrome": { "color": "#dddddd", "metallic": 1, "roughness": 0.15 },
  "neon":  { "color": "#00e5ff", "emissive": 2.0 },
  "flat":  { "color": "#222222", "unlit": true }
}
```

| key | default | notes |
|---|---|---|
| `color` | `#e7e7e7` | base color |
| `roughness` | 0.6 | 0 = glossy highlight, 1 = matte |
| `metallic` | 0 | 1 = metal: tinted highlight, reflects the sky |
| `emissive` | none | a number makes it glow in its own color (1–3 is a good range), or a `"#color"` |
| `unlit` | false | shows the color exactly, no lighting |

## objects

Every object has an `id` and at most one of `mesh`, `light` or `camera` (a mesh object with an `emitter` block becomes many instances, see [Emitters](#emitters)). Shared keys:

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

### glTF models

`"mesh": "models/chair.glb"` (or `.gltf`, path relative to the scene file) imports the whole file as one static mesh.

- **Axes:** glTF is Y-up, the engine is Z-up. A model comes in with glTF +Y → up (+Z), glTF +Z (its front) → +X (the engine's forward), glTF +X → −Y. So a glTF character facing +Z faces +X here, like the rest of this format.
- **No recentering or rescaling:** units stay meters and the origin stays where it was modeled. Use `pivot` (`"bottom"` or `"center"`) to rest it on the floor or spin it around its middle, and `scale` to size it.
- **Materials:** base color (factor and texture), metallic/roughness (factors and texture), normal map, occlusion, emissive (factor, texture, `KHR_materials_emissive_strength`), `KHR_materials_unlit`, vertex colors, alpha mask cutouts and double-sided. Textures can be embedded (`.glb`, data URIs) or image files next to the model. `KHR_texture_transform` is ignored. No skinning or animations (animate the object instead), no cameras or lights from the file.
- `material` or `color` on the object replaces the model's materials with one of ours.
- Prefer textures with a plain `LINEAR` min filter (9729); some drivers have rendered mipmapped samplers black.

A minimal textured model is in `examples/assets/crate.gltf` (a 1 m cube resting on its own origin), used by `examples/textured.json`.

### Light objects

```json
{ "id": "sun", "light": { "type": "directional", "intensity": 1.1 }, "position": [5, -6, 7], "lookAt": [0, 0, 0] }
```

| key | default | notes |
|---|---|---|
| `type` | `directional` | `directional` (sun: only its direction matters), `point` (bulb), `spot` (cone) |
| `color` | white | |
| `intensity` | 1.1 / 40 / 60 | defaults per type; point and spot lights fade with distance, so they need large values |
| `radius` | 20 | point/spot: distance where the light reaches zero |
| `innerAngle`, `outerAngle` | 20, 35 | spot: full-strength and edge cone half-angles in degrees |

Lights point along their forward axis, so aim them with `lookAt` (or `rotation`). A directional light's position only matters for `lookAt`.

### The camera

Exactly one object with `"camera": {}`. Aim it with `lookAt`. The field of view is fixed at **90° vertical**, which is wide: to fill most of the frame with an object of height `h`, keep the camera about `0.8 × h` to `1.2 × h` away from it. A camera slightly above the subject looking down 15–30° looks best.

### Emitters

A mesh object with an `emitter` block becomes many instances of its mesh: falling petals, snow, sparks, bubbles (with `motion`), or grass, stones, a crowd of trees (static scatter, without `motion`). It knows nothing about what it emits; those are just parameters. Use it instead of writing hundreds of objects or baking motion.

```json
{ "id": "petals", "mesh": "sphere", "detail": 1, "color": ["#f9c5d3", "#f4a6bc", "#fde3ea"],
  "emitter": { "count": 300, "area": { "min": [-5, -5, 0], "max": [5, 5, 4] },
               "size": [0.07, 0.11], "scale": [1, 0.7, 0.18], "rotation": "random",
               "motion": { "fall": 0.45, "drift": [0.25, 0.15, 0], "sway": { "amplitude": 0.3, "period": 2.4 }, "spin": [60, 240] },
               "seed": 7 } },
{ "id": "grass", "mesh": "cone", "detail": 4, "flat": true, "pivot": "bottom", "color": ["#9cc47a", "#86b46a"],
  "emitter": { "count": 1400, "area": { "center": [0, 0, 0], "radius": 9 }, "size": [0.8, 1.4], "scale": [0.07, 0.07, 0.26],
               "rotation": "yaw", "minGap": 0.14, "avoid": [{ "center": [0, 0], "radius": 0.6 }] } }
```

The object keeps its usual mesh keys: `mesh` (any built-in shape or a glTF file), shape options (`detail`, `flat`, `bevel`, ...), `pivot`, `material`. `color` can be **a list**: each instance picks one at random. Its `position`, `rotation`, `scale` and `parent` place the **whole emitter** (the area is in its own space), and tracks on its `position`, `rotation`, `scale` and `visible` move or hide all instances together; instances keep their colors. Instances cast **no shadows** unless `"shadow": true`.

| `emitter` key | default | notes |
|---|---|---|
| `count` | required | instances, 1–20000 |
| `area` | required | where they are: a box `{ "min": [x, y, z], "max": [x, y, z] }`, or a disc `{ "center": [x, y, z], "radius": r }` with optional `inner` (a ring) and `height` (a cylinder going up from `center`). A flat area (min z = max z, or no height) puts every instance at that height |
| `size` | 1 | uniform scale per instance: a number, or `[min, max]` for a random size in between |
| `scale` | 1 | per-instance scale `[x, y, z]` before `size`: `[1, 0.7, 0.18]` flattens a sphere into a petal |
| `rotation` | `[0, 0, 0]` | `"random"` (any orientation), `"yaw"` (random turn around Z, stays upright: grass, rocks, trees) or a fixed `[pitch, yaw, roll]` |
| `seed` | 1 | the same seed always gives the same instances; change it for a different arrangement |
| `motion` | none | makes them move (below). Without it the instances are a static scatter |
| `loop` | true | with `motion`: an instance leaving the area re-enters at the far side, so the area stays full for the whole clip. `false`: falling ones settle at the bottom of the area and stop there (rising ones at the top) |
| `minGap` | 0 | static only: horizontal distance kept between instance centers. If there is no room left, fewer are placed and the render prints a note |
| `avoid` | none | static only: horizontal regions kept clear, `[{ "center": [x, y], "radius": r }, { "min": [x, y], "max": [x, y] }]` (around a tree trunk, a path, a house) |

| `motion` key | notes |
|---|---|
| `fall` | meters per second downward (negative rises: bubbles, embers) |
| `drift` | `[x, y, z]` meters per second, added to the fall (wind) |
| `sway` | `{ "amplitude": meters, "period": seconds }`: side-to-side flutter, each instance in its own direction, phase and a slightly different period (default period 2) |
| `spin` | degrees per second around a random axis per instance, a number or `[min, max]` (the direction is random too) |

Looping instances travel through the area's height when they fall or rise (so the area needs a height), or across it along the drift when they only drift. Each pass starts from a fresh random spot, centered so that drift doesn't empty one side. Everything is a function of time: `--frame`, `--times` and full renders show the same positions.

**Performance:** instances are cheap. Even the software renderer (servers and cloud sessions without a GPU) renders 3000 moving petals at full quality in about 0.2 s per frame, and 20000 static grass blades in about 1 s per frame. The limit is 20000 per emitter; keep moving ones to a few thousand.

## animations

Each track sets one property of one object over time. `target` is `"id.property"` or `"id.property.x|y|z"` for one component.

Properties: `position`, `rotation`, `scale` (vectors), `color` (meshes and lights), `intensity` (lights), `visible` (true/false), `lookAt` (a point; replaces an object target).

Each track has exactly one of `keys`, `spin`, `wave`, `orbit` (basics) or `follow`, `path`, `bounce`, `walk` (motion helpers):

**keys**: values at times, eased in between. A keyed track does nothing before its first key and holds its last value after its last key. So you can split a story into beats: several keyed tracks on the same property, each starting where the previous one ends (start each one with the value the previous one ended on).

```json
{ "target": "ball.position.z", "keys": [[0, 0.5], [0.4, 2.5, "inQuad"], [0.8, 0.5]], "ease": "outQuad" }
```

Each key is `[time, value]` or `[time, value, "ease"]`; the ease shapes the segment that *starts* at that key, and `"ease"` on the track is the default (`inOutCubic` if unset). Vector values can also be written flat, `[time, x, y, z]` or `[time, x, y, z, "ease"]` (same as `[time, [x, y, z]]`): the compact form generated tracks use.

**Repeating keys:** `"repeat": true` plays the key range (first to last key) over and over, from the first key on. `"period": seconds` repeats it every `period` seconds instead, holding the last value in between (blink every 2 s: keys over 0.2 s, `"period": 2`). For a seamless loop make the last value equal the first.

```json
{ "target": "tip.scale", "keys": [[1, 0.1], [1.1, 0.16], [1.3, 0.1]], "period": 1 }
```

Eases: `linear`, `step`, `inQuad`, `outQuad`, `inOutQuad`, `inCubic`, `outCubic`, `inOutCubic`, `inSine`, `outSine`, `inOutSine`, `inBack`, `outBack`, `inOutBack`, `outBounce`, `outElastic`. `outBack` and `outElastic` overshoot, which is great for pop-in scale animations.

**spin**: adds a constant rotation speed in degrees per second, `[pitch, yaw, roll]`.

```json
{ "target": "box.rotation", "spin": [0, 90, 0] }
```

**wave**: adds `amplitude × sin(2π × (t / period + phase))` on top of the value. Good for bobbing, breathing, pulsing.

```json
{ "target": "ufo.position.z", "wave": { "amplitude": 0.2, "period": 1.5 } }
{ "target": "heart.scale", "wave": { "amplitude": 0.08, "period": 0.6 } }
```

**orbit**: moves a position on a horizontal circle. Perfect for camera turntables (combine with `lookAt`).

```json
{ "target": "cam.position", "orbit": { "center": [0, 0, 0], "radius": 4, "height": 2, "period": 8, "startAngle": -120 } }
```

`period` is seconds per full turn (negative turns the other way); `startAngle` is in degrees around Z, 0 = on the +X side.

**follow**: sets a `position` or `lookAt` to another object's world position plus an `offset` (world axes). `smoothing` (seconds, default 0) makes it trail behind like a camera operator; 0.3–0.6 feels natural. Tracking shot:

```json
{ "target": "cam.position", "follow": { "object": "bot", "offset": [0.5, 1.6, 1.1], "smoothing": 0.5 } },
{ "target": "cam.lookAt",   "follow": { "object": "bot", "offset": [0, 0, 0.7], "smoothing": 0.25 } }
```

**path**: moves a `position` (or a `lookAt` point) through points at an even speed.

```json
{ "target": "car.position", "path": { "points": [[2, 0, 0], [0, 2, 0], [-2, 0, 0], [0, -2, 0]], "loop": true, "duration": 6, "face": true } }
```

| key | default | notes |
|---|---|---|
| `points` | required | `[[x, y, z], ...]`, at least 2 |
| `curve` | `smooth` | `smooth` passes through every point, `linear` goes straight between them, `bezier` takes `point, handle, handle, point, ...` (3n + 1 points; 3n when looping) |
| `start`, `duration` | 0, to the end of the clip | before `start` the object waits at the first point, after the end it stays at the last |
| `loop` | false | closes the path and goes around again every `duration` |
| `face` | false | `position` paths: turns the object (yaw) along its direction of travel. It adds to the object's own yaw, so use `rotation` to correct a model that doesn't face +X |
| `ease` | `linear` | over the whole trip (`inOutSine` starts and stops gently) |

**bounce**: hops added on top of a `position.z` (its resting height). Use `"pivot": "bottom"` on the object so the squash stays on the floor.

```json
{ "target": "ball.position.z", "bounce": { "height": 1.5, "period": 0.8, "squash": 0.25, "restitution": 0.7 } }
```

| key | default | notes |
|---|---|---|
| `height` | 1 | top of the first hop, above the resting height |
| `period` | 1 | seconds per hop |
| `restitution` | 1 | 1 hops forever (loops); below 1 each hop is lower and shorter until it rests (0.6–0.8 looks real) |
| `squash` | 0 | squash at impact, stretch while fast, round at the top (0.15–0.3); also scales the object |
| `start` | 0 | time of the first ground contact; before it the object rests. Use `-period / 2` to start at the top |

**walk**: a walk cycle on a rig of joint groups (see `examples/character.json`). The target is the rig's root id alone. Joints are pairs, left then right; any can be left out.

```json
{ "target": "bot", "walk": { "legs": ["hipL", "hipR"], "knees": ["kneeL", "kneeR"], "feet": ["ankleL", "ankleR"],
                             "arms": ["shoulderL", "shoulderR"], "period": 1.1, "swing": 22, "start": 0, "end": 3 } }
```

| key | default | notes |
|---|---|---|
| `legs`, `knees`, `feet`, `arms` | | joint ids. Legs swing with pitch, knees bend while their leg swings forward, feet stay level, arms swing opposite to the legs |
| `period` | 1 | seconds per full stride (left and right step) |
| `swing`, `kneeBend`, `armSwing` | 25, 40, 20 | degrees |
| `bob` | 0.03 | how far the root dips at full stride; about leg length × (1 − cos swing) keeps the feet on the floor |
| `start`, `end` | 0, end of clip | eases in and out over 0.25 s |

The walk doesn't move the character: pair it with a `path` (with `face`) on the root. To keep feet from sliding, travel about 4 × leg length × sin(swing) per period.

Tracks apply in order: `keys`, `orbit`, `follow` and `path` set the value, `spin`, `wave`, `bounce` and `walk` add to whatever is there, so a `wave` after a `keys` track bobs around the keyed motion. Put `walk` after any keyed tracks on the same joints.

## Python scenes (.py)

When a scene has many objects, scattered placement or procedural shapes, write a Python script that prints the scene JSON instead of the JSON itself. `cerender.py` renders it the same way (`render village.py --preview`): it runs the script with the same Python, renders what it printed from a temp file (nothing big is written next to the script), and resolves glTF paths relative to the script's folder. If the script fails you get its traceback; renderer errors still give JSON paths (`objects[12].emitter.count`). The script must print only the JSON to stdout (debug prints go to `sys.stderr`). To keep the JSON (to share it, or render it without Python) run `python scene.py > scene.json` yourself.

`cerender_lib` (standard library only) is importable from the script without setup. It provides **tools, not things**: builders that map 1:1 onto this format, plus placement and path helpers. Keyword arguments use this file's key names; snake_case works too (`look_at` → `lookAt`, `min_gap` → `minGap`).

```python
from cerender_lib import Scene, scatter, look_along

s = Scene(duration=6, width=1080, height=1350)
s.environment(sky=["#9dbde3", "#eef3f8", "#e4ebf2"], ground="#eef2f7")
for i, (x, y, z) in enumerate(scatter(12, {"min": [-14, -6, 0], "max": [14, 6, 0]}, min_gap=3.6, avoid=[{"min": [-30, -3], "max": [30, 3]}])):
    s.add(f"house{i}", mesh="roundbox", color="#e8a87c", pivot="bottom", position=[x, y, 0])
s.emitter("snow", "sphere", 900, {"min": [-12, -6, 0], "max": [12, 6, 7]}, size=0.04, motion={"fall": 0.9})
s.light("sun", position=[3, -6, 6])
s.camera("cam")
s.animate(look_along("cam", [[-14, 0, 2.8], [0, 0, 2.4], [8, 0, 2.8]], duration=6))
s.write()
```

| | |
|---|---|
| `Scene(duration=0, fps=30, width=1080, height=1080)` | the scene. Builders: `environment(**keys)`, `material(name, **keys)`, `add(id, **keys)` (any object, keys as in this file), `group(id, **keys)`, `light(id, type="directional", position, look_at, **keys)`, `camera(id="cam", position, look_at, **keys)`, `emitter(id, mesh, count, area, size, scale, rotation, motion, loop, min_gap, avoid, seed, **object_keys)`, `animate(target, **track)` or `animate(track_or_tracks)`. `write()` prints compact JSON with numbers rounded to 4 decimals; call it last |
| `rng(seed)` | a seeded `random.Random`, so every run builds the same scene |
| `scatter(count, area, min_gap=0, avoid=(), seed=1)` | up to `count` points `[x, y, z]`, areas and avoid regions written like an emitter's. For placing *different* things (houses built from parts, trees with random heights). For identical copies use an emitter: it's one object instead of hundreds |
| `circle(center, radius, count, start=0)` | points around a horizontal circle |
| `smooth(points, samples=8, loop=False)` | points along the same smooth curve `path` follows, for placing things along it (fence posts, lamps) |
| `path(target, points, duration=None, start=0, loop=False, face=False, curve="smooth", ease=None)` | a `path` track (`target` is an id, or `"id.lookAt"`) |
| `look_along(camera, points, duration, start=0, ahead=0.6, loop=False)` | a fly-through: two `path` tracks, the camera's `lookAt` running `ahead` seconds in front. Keep the path fairly level: a climbing path looks up |
| `bake(target, fn, start, end, fps=30)` | a `keys` track sampling `fn(t)` (a number or `[x, y, z]`), in the compact key form, dropping keys a straight line already gives. **Last resort**: only for motion no feature covers |

Prefer engine features to code that computes motion: emitters for particles and identical scattered copies, `repeat`/`period` and `wave` for cycles, `path` and `look_along` for travel, `walk` for characters. A baked track is a key per frame per object; 300 baked petals are about 1 MB even in the compact form, the emitter version is one object of a few hundred bytes.

## Gotchas

- **Flat meshes lying on the ground hide ground shadows.** Shadows are drawn on the ground itself, so a road, path or rug (a `plane` or thin box at z ≈ 0) covers them. Keep such pieces small, or accept no shadows on them.
- **Keep surfaces at least 3 cm apart.** Faces closer than that (a decal on a box, a belt on a body) can flicker or show through, especially far from the camera. Make the outer part a bit bigger instead of nearly touching.
- **Non-uniform parent scale doesn't shear rotated children.** Scaling a parent `[1, 3, 1]` scales a rotated child along the child's own axes, not the parent's: build stretched parts by scaling the part, not its group.
- **The ground renders lighter than its hex** (sun plus ambient light it). For a seamless backdrop make it a bit darker than the sky's bottom color; `examples/studio.json` has a matched set.
- **Point lights leave a bright patch on the ground** near them. Keep them high, small in `radius`, or away from the floor.
- **Strong emissive washes colors out** toward white (cyan, pink). Keep `emissive` ≤ 1.5 for saturated colors.
- The field of view is a wide 90°: things look smaller than you expect. Get the camera close.

## Errors

The renderer checks the whole file before drawing and prints every problem with its JSON path, for example:

```
error: 2 problem(s) in scene.json
  - objects[1]: unknown key "postion" (did you mean "position"?)
  - animations[0].target: no object with id "bal"
```
