# Emitters

Part of the scene reference: the core (structure, objects, orientation, facts, gotchas) is in `reference.md`.

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

- **Any mesh works as the instance mesh:** every built-in shape (`lathe`, `rock`, `gem`, `roundbox`, ...) and `.glb`/`.gltf` files (which keep their own materials unless the object sets `material` or `color`).
- **`parent` and tracks work:** a moving parent carries the whole emitter. `visible` keys hide all instances (see tracks.md for how `visible` keys switch). Like any object, a hidden parent doesn't hide it.
- **Glow color:** a numeric `emissive` comes from the material's own color, not from the color list, so every instance glows in that one color. For colored sparks give the emitter a material whose `color` is the glow color, or use one emitter per glow color.
- **Motion is a function of clip time from t = 0**, continuous and loop-based: there are no bursts and no change of size or color over an instance's life. Workarounds are in reference.md, Facts & limits.

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
