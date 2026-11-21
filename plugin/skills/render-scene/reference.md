# Scene file reference (version 1)

One JSON file describes everything. Comments (`//`) are allowed. Unknown keys are errors, with a "did you mean" hint.

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

Every object has an `id` and exactly one of `mesh`, `light` or `camera`. Shared keys:

| key | default | notes |
|---|---|---|
| `id` | required | unique name, used by `parent`, `lookAt` and animations |
| `position` | `[0, 0, 0]` | relative to the parent, if there is one |
| `rotation` | `[0, 0, 0]` | `[pitch, yaw, roll]` degrees, relative to the parent |
| `scale` | 1 | number or `[x, y, z]` |
| `parent` | none | id of another object; this object then moves, turns and scales with it |
| `lookAt` | none | `[x, y, z]` or an object id; turns the object to face it every frame (cameras, lights, heads) |
| `visible` | true | |

### Mesh objects

| key | notes |
|---|---|
| `mesh` | `cube`, `sphere`, `cylinder`, `cone`, `plane`, or a path to a `.glb`/`.gltf` file relative to the scene file |
| `material` | name from `materials`, or an inline material object |
| `color` | shortcut: the material (or the default one) in this color |
| `shadow` | casts a shadow on the ground, default true (false for planes) |

Cylinders and cones stand upright along Z. A `plane` lies flat in XY (use rotation to stand it up). Imported glTF files keep their own materials and textures unless `material` or `color` is set.

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

## animations

Each track sets one property of one object over time. `target` is `"id.property"` or `"id.property.x|y|z"` for one component.

Properties: `position`, `rotation`, `scale` (vectors), `color` (meshes and lights), `intensity` (lights), `visible` (true/false), `lookAt` (a point; replaces an object target).

Each track has exactly one of:

**keys**: values at times, eased in between. A keyed track does nothing before its first key and holds its last value after its last key. So you can split a story into beats: several keyed tracks on the same property, each starting where the previous one ends (start each one with the value the previous one ended on).

```json
{ "target": "ball.position.z", "keys": [[0, 0.5], [0.4, 2.5, "inQuad"], [0.8, 0.5]], "ease": "outQuad" }
```

Each key is `[time, value]` or `[time, value, "ease"]`; the ease shapes the segment that *starts* at that key, and `"ease"` on the track is the default (`inOutCubic` if unset). Eases: `linear`, `step`, `inQuad`, `outQuad`, `inOutQuad`, `inCubic`, `outCubic`, `inOutCubic`, `inSine`, `outSine`, `inOutSine`, `inBack`, `outBack`, `inOutBack`, `outBounce`, `outElastic`. `outBack` and `outElastic` overshoot, which is great for pop-in scale animations.

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

Tracks apply in order: `keys` and `orbit` set the value, `spin` and `wave` add to whatever is there, so a `wave` after a `keys` track bobs around the keyed motion.

## Errors

The renderer checks the whole file before drawing and prints every problem with its JSON path, for example:

```
error: 2 problem(s) in scene.json
  - objects[1]: unknown key "postion" (did you mean "position"?)
  - animations[0].target: no object with id "bal"
```
