---
name: render-scene
description: Render 3D images and short animated clips from a JSON scene (or a Python script that prints one) with the ClubEngine renderer. Use when the user wants a 3D render, a looping 3D animation, a product turntable, particles like falling petals or snow, or a small animated 3D story (Instagram/LinkedIn-style clips), built from simple shapes or glTF models.
---

# Render scenes with ClubEngine

You describe a scene in one JSON file (or a short Python script that prints it); a real engine renders it to PNG frames, MP4 or GIF. You look at the result and iterate.

## Cheat sheet

**Docs** in `${CLAUDE_PLUGIN_ROOT}/skills/render-scene/`. Read `reference.md` (core: structure, objects, orientation, **Facts & limits**, gotchas) before your first scene, then **only** the topic files the scene needs: `tracks.md` (any animation), `emitters.md` (particles, scatter), `materials-lighting.md` (materials, emissive, point/spot lights), `camera.md` (camera moves), `gltf.md` (models), `python-lib.md` (`.py` scenes), `cli.md` (launcher details, ffmpeg, Linux). Don't read `scripts/*.py`: everything is here.

**CLI**: `python "${CLAUDE_PLUGIN_ROOT}/scripts/cerender.py" render SCENE [flags]` (SCENE is `.json` or `.py`; `python3` if `python` isn't found)

| flag | |
|---|---|
| `--preview` | 6 moments on a labeled contact sheet, half size, tiles ≤ 240 px |
| `--times 3.5,3.95` | contact sheet of these moments (seconds) |
| `--range A-B [--step S]` | contact sheet from A to B every S s (default 6 moments, max 48) |
| `--frame N` | one full-quality PNG; N = 0 … duration × fps − 1 |
| `--size W` or `WxH` | output size, any mode (a width keeps the aspect ratio) |
| `--mp4`, `--gif` | encode the whole clip (ffmpeg found or downloaded first) |
| `--out PATH` | frames folder, or the video file if it ends in `.mp4`/`.gif` |
| `--keep-frames` | with `--mp4`/`--gif`, also keep the PNG frames |
| `--ffmpeg PATH` | ffmpeg to use (else `CERENDER_FFMPEG`, `PATH`, a one-time download) |

One of `--preview` / `--times`·`--range` / `--frame` per run; `--mp4`/`--gif` go alone. The first run downloads the renderer (~3 MB) once; `cerender.py setup` fetches it and ffmpeg up front.

**cerender_lib** (`.py` scenes; keyword names as in the format, snake_case works):

```
Scene(duration=0, fps=30, width=1080, height=1080)   the scene; call .write() last (prints JSON)
  .environment(**keys) .material(name, **keys) .add(id, **keys) .group(id, **keys)
  .light(id, type="directional", position=(5,-6,7), look_at=(0,0,0), **keys)
  .camera(id="cam", position=(4,-4,2.5), look_at=(0,0,0.5), **keys)
  .emitter(id, mesh, count, area, size, scale, rotation, motion, loop, min_gap, avoid, seed, **object_keys)
  .animate(target, **track) | .animate(track_or_list)    .to_dict(digits=4)  .write(digits=4)
rng(seed=1)                                          seeded random.Random
scatter(count, area, min_gap=0, avoid=(), seed=1)    up to count points [x,y,z] for placing different things
circle(center=(0,0,0), radius=1, count=8, start=0)   points around a horizontal circle
smooth(points, samples=8, loop=False)                points along the curve path follows
path(target, points, duration=None, start=0, loop=False, face=False, curve="smooth", ease=None)  path track
look_along(camera, points, duration, start=0, ahead=0.6, loop=False)  fly-through: 2 tracks
bake(target, fn, start, end, fps=30)                 keys track sampling fn(t): last resort
```

**Workflow**: write the scene (`.json` or `.py`) in the user's folder, then:
1. **Layout:** `--preview` (or `--times`): framing, colors, overlaps, things floating or sinking into the ground. Tiles are small: don't judge detail on them.
2. **Detail:** `--frame N --size 960` on the frames that matter. Full size only when fine detail matters.
3. **Motion:** stills hide snaps and pops. Around **every contact, hit, appear/disappear and fast move**, render consecutive frames: `--range 11.1-11.4 --step 0.0333` (30 fps). Look for jumps in position or orientation, interpenetration, objects popping in or out, the camera clipping into geometry.
4. **Fix and repeat**, usually 1–3 rounds. Errors come all at once with JSON paths: fix them all, then rerun.
5. **Final:** `--mp4` (add `--gif` for a GIF), 2× supersampled, streamed into ffmpeg. If ffmpeg can't be found or fetched the script prints a one-line install command: give it to the user. Report the printed paths (`mp4:`, `gif:`, `image:`); don't paste frames beyond one look you needed.

## Examples

Start from the closest one in `${CLAUDE_PLUGIN_ROOT}/skills/render-scene/examples/`:

- `hello.json`: a still of three shapes on a pastel floor, the minimal scene
- `studio.json`: seamless pastel studio backdrop (tested sky/ground/ambient values), floating gem loop
- `bounce.json`: looping ball bounce with squash and stretch (`bounce` track, `pivot: bottom`)
- `turntable.json`: product turntable, camera orbits a trophy (lathe cup, gem, metals)
- `robot.json`: a character from primitives with groups as joints: pop in, hop, wave, spin
- `character.json`: **character template**: robot rig with hips, knees, feet and arms; ready-made walk, wave and kick; tracking camera
- `textured.json`: glTF models with a texture (`assets/crate.gltf`)
- `petals.json`: **emitters**: falling cherry petals that loop, plus grass and fallen petals scattered with `minGap` and `avoid`
- `village.py`: **Python scene** with `cerender_lib`: houses built at `scatter()` spots, a pine emitter that avoids them, snowfall, a `look_along` fly-through

## JSON or a Python script?

- **Write JSON directly** for small scenes: a character, a product shot, a few dozen objects, hand-placed beats.
- **Write a `.py` script** with `cerender_lib` when there are many objects, scattered placement or anything procedural (a village, a forest of different trees, objects along a curve). Render it with `cerender.py` like a `.json` (never `python scene.py` directly). Details in `python-lib.md`.
- **Particles and identical scattered copies are emitters**, in either form: petals, snow, rain, sparks, bubbles, grass, pebbles, a field of identical trees. One emitter object replaces hundreds of objects and every baked key. Don't write per-particle objects or bake their motion.
- Prefer engine motion over computed keys: `repeat`/`period`, `wave`, `path`/`look_along`, `follow`, `walk`, `bounce`, emitter `motion`. Bake (`cerender_lib.bake`) only what none of them covers.
- **Never read or open generated JSON** (a `.py` scene's output, or a big file a script wrote). It's huge and tells you less than the script. Errors point to JSON paths like `objects[12].emitter.count`: find the code that made that object and fix the script.

## Getting good-looking results

- **Coordinates:** Z up, ground at z = 0, meters, characters face +X. Built-in meshes are 1 unit and centered; `"pivot": "bottom"` puts the origin at the base so z = 0 rests on the floor.
- **Camera:** the field of view is a wide 90° vertical. Get close: about 1× the subject's height away, a bit above, looking down 15–30° with `lookAt`. Orbits, slow push-ins and `follow` tracking shots look premium.
- **Light:** one directional `sun` from the side and above (e.g. `[5, -6, 7]` with `lookAt` the subject) gives readable shapes and a visible ground shadow. From behind the camera it flattens everything.
- **Palette:** 3–5 harmonious colors. For a seamless studio backdrop copy `studio.json`'s environment; for a diorama use a contrasting ground.
- **Shapes:** beyond cube/sphere/cylinder/cone: `roundbox` (soft edges), `torus`/`ring`, `gem`, `rock`, `lathe` (vases, cups, bottles from a profile), and `"flat": true` for a faceted low-poly look.
- **Particles and scatter:** an emitter for anything there are many identical copies of. Give moving ones a palette (`"color": [...]`), `sway` and `spin` so they don't look mechanical; a little `emissive` keeps small particles readable against a bright sky.
- **Motion:** ease everything (`inOutCubic`, `outBack` for pop-ins). Use the helpers instead of hand-keying: `bounce` (hops with squash), `path` (travel with `face`), `follow` (camera tracking), `walk` (leg/arm cycle), `"repeat"`/`"period"` for looping keys. Loops: full spin/orbit periods that divide the duration.
- **Characters:** start from `character.json`. Joints are groups (objects with no mesh); parts hang below them. Squash the root, not one part.
- **Clip length:** 3–8 s at 30 fps. Portrait 1080×1350 works for Instagram and LinkedIn feeds.

## Limits

- Shadows fall only on the ground (from the first directional light); objects don't shadow each other.
- No transparency, bloom, text, or skeletal animation (glTF models import as static meshes: animate them as whole objects or build rigs from parts). Workarounds are in reference.md, Facts & limits. Captions can be added to the MP4 afterwards with ffmpeg's `drawtext`.
- One camera, fixed 90° vertical field of view.
- Windows and Linux (x64) only; headless Linux setup is in `cli.md`.
