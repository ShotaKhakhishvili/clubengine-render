# Python scenes (.py) and cerender_lib

Part of the scene reference: the core (structure, objects, orientation, facts, gotchas) is in `reference.md`.

When a scene has many objects, scattered placement or procedural shapes, write a Python script that prints the scene JSON instead of the JSON itself. `cerender.py` renders it the same way (`render village.py --preview`): it runs the script with the same Python, renders what it printed from a temp file (nothing big is written next to the script), and resolves glTF paths relative to the script's folder. If the script fails you get its traceback; renderer errors still give JSON paths (`objects[12].emitter.count`). The script must print only the JSON to stdout (debug prints go to `sys.stderr`). To keep the JSON (to share it, or render it without Python) run `python scene.py > scene.json` yourself.

`cerender_lib` (standard library only) is importable from the script without setup. It provides **tools, not things**: builders that map 1:1 onto the scene format, plus placement and path helpers. Keyword arguments use the format's key names; snake_case works too (`look_at` → `lookAt`, `min_gap` → `minGap`).

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
| `Scene(duration=0, fps=30, width=1080, height=1080)` | the scene. Builders: `environment(**keys)`, `material(name, **keys)`, `add(id, **keys)` (any object, keys as in the format), `group(id, **keys)`, `light(id, type="directional", position, look_at, **keys)`, `camera(id="cam", position, look_at, **keys)`, `emitter(id, mesh, count, area, size, scale, rotation, motion, loop, min_gap, avoid, seed, **object_keys)`, `animate(target, **track)` or `animate(track_or_tracks)`. `write()` prints compact JSON with numbers rounded to 4 decimals; call it last |
| `rng(seed)` | a seeded `random.Random`, so every run builds the same scene |
| `scatter(count, area, min_gap=0, avoid=(), seed=1)` | up to `count` points `[x, y, z]`, areas and avoid regions written like an emitter's. For placing *different* things (houses built from parts, trees with random heights). For identical copies use an emitter: it's one object instead of hundreds |
| `circle(center, radius, count, start=0)` | points around a horizontal circle |
| `smooth(points, samples=8, loop=False)` | points along the same smooth curve `path` follows, for placing things along it (fence posts, lamps) |
| `path(target, points, duration=None, start=0, loop=False, face=False, curve="smooth", ease=None)` | a `path` track (`target` is an id, or `"id.lookAt"`) |
| `look_along(camera, points, duration, start=0, ahead=0.6, loop=False)` | a fly-through: two `path` tracks, the camera's `lookAt` running `ahead` seconds in front. Keep the path fairly level: a climbing path looks up |
| `bake(target, fn, start, end, fps=30)` | a `keys` track sampling `fn(t)` (a number or `[x, y, z]`), in the compact key form, dropping keys a straight line already gives. **Last resort**: only for motion no feature covers |

Prefer engine features to code that computes motion: emitters for particles and identical scattered copies, `repeat`/`period` and `wave` for cycles, `path` and `look_along` for travel, `walk` for characters. A baked track is a key per frame per object; 300 baked petals are about 1 MB even in the compact form, the emitter version is one object of a few hundred bytes.

Other entry points (in `scripts/cerender_lib.py`): `Scene.to_dict(digits=4)` returns the scene as a dict without printing it; `write(digits=4)` sets the rounding; `scatter(..., attempts=30)` sets how many tries per point before giving up (it prints a `placed N of M` note to stderr when `min_gap`/`avoid` leave no room); `bake(..., tolerance=1e-3)` sets how far a dropped key may be off a straight line.

**Always render a `.py` through `cerender.py`** (`render scene.py --preview`). It puts `cerender_lib` on the path. Running `python scene.py` directly fails with `ModuleNotFoundError: No module named 'cerender_lib'` unless `PYTHONPATH` points at the plugin's `scripts/` folder: `PYTHONPATH="${CLAUDE_PLUGIN_ROOT}/scripts" python scene.py > scene.json`.
