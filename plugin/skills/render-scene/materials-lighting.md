# Materials and lighting

Part of the scene reference: the core (structure, objects, orientation, facts, gotchas) is in `reference.md`.

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

- These keys exist **only in a material**: a named one in `materials`, or inline on the object as `"material": { "color": "#f80", "roughness": 0.3 }`. `roughness`, `metallic`, `emissive` or `unlit` directly on an object is an "unknown key" error.
- `color` on an object replaces only the material's base color and keeps its other keys.
- A numeric `emissive` is the material's own `color` × the number, fixed when the scene loads: an object `color`, an emitter color list or a `color` track don't change the glow color. Give each glow color its own material, or write `"emissive": "#color"`.
- Brightness, clipping and fog on emissive/unlit: see reference.md, Facts & limits.

## Light objects

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

- There is no limit on the number of lights (see reference.md, Facts & limits), but every point/spot light costs time on every pixel.
- Only the first directional light casts the ground shadows; point and spot lights cast none.
- Emissive surfaces don't light anything around them. To make a lamp or neon sign light the scene, put a point light next to it in the same color.

## Lighting tips

- One directional `sun` from the side and above (e.g. `[5, -6, 7]` with `lookAt` the subject) gives readable shapes and a visible ground shadow. From behind the camera it flattens everything.
- Lower the sun for long dramatic shadows, raise it for short ones (reference.md, environment).
- Point lights leave a bright patch on the ground near them (reference.md, Gotchas).
