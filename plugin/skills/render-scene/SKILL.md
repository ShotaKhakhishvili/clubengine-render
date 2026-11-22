---
name: render-scene
description: Render 3D images and short animated clips from a JSON scene with the ClubEngine renderer. Use when the user wants a 3D render, a looping 3D animation, a product turntable, or a small animated 3D story (Instagram/LinkedIn-style clips), built from simple shapes or glTF models.
---

# Render scenes with ClubEngine

You describe a scene in one JSON file; a real engine renders it to PNG frames, MP4 or GIF. You look at the result and iterate.

**Read `${CLAUDE_PLUGIN_ROOT}/skills/render-scene/reference.md` before writing your first scene.** It has every key, the orientation cheat sheet (which rotation sign moves a limb where, which screen side +X/+Y land on) and the gotchas. Start from the closest example in `${CLAUDE_PLUGIN_ROOT}/skills/render-scene/examples/`:

- `hello.json`: a still of three shapes on a pastel floor, the minimal scene
- `studio.json`: seamless pastel studio backdrop (tested sky/ground/ambient values), floating gem loop
- `bounce.json`: looping ball bounce with squash and stretch (`bounce` track, `pivot: bottom`)
- `turntable.json`: product turntable, camera orbits a trophy (lathe cup, gem, metals)
- `robot.json`: a character from primitives with groups as joints: pop in, hop, wave, spin
- `character.json`: **character template**: robot rig with hips, knees, feet and arms; ready-made walk, wave and kick; tracking camera
- `textured.json`: glTF models with a texture (`assets/crate.gltf`)

## Workflow

Use `python3` if `python` isn't found. The first run downloads the renderer (~3 MB) once.

1. **Write the scene** to a `.json` file in the user's working folder.
2. **Preview**: 6 moments of the clip on a small contact sheet, each tile labeled with its time:
   ```bash
   python "${CLAUDE_PLUGIN_ROOT}/scripts/cerender.py" render scene.json --preview
   ```
3. **Look at it.** Read the `contact sheet:` path it prints (or the `image:` for a still). Check framing, colors, motion, overlaps, things floating or sinking into the ground. To check a beat closely, render just those moments instead of the whole clip:
   - `--times 3.5,3.95,5.5` or `--range 3.5-5 --step 0.1`: labeled contact sheet of those moments
   - `--frame 105`: one full-quality frame, to see details
   - `--size 540` (width) or `--size 540x675` works with any of them

   Fix and preview again until it looks right; usually 1–3 rounds.
4. **Final render**:
   ```bash
   python "${CLAUDE_PLUGIN_ROOT}/scripts/cerender.py" render scene.json --mp4
   ```
   Add `--gif` for a GIF, `--out path/clip.mp4` to choose where the video goes. Final renders are 2× supersampled and stream straight into the MP4 (no PNG frames unless you add `--keep-frames`); the contact sheet is still written. ffmpeg is found or fetched **before** rendering (PATH, `--ffmpeg PATH`, `CERENDER_FFMPEG`, else a one-time download into the renderer's cache). If that fails the script stops with a one-line install command: give it to the user; don't try to install ffmpeg some other way.
5. **Report** the full file paths it printed (`mp4:`, `gif:`, `image:`). Don't paste frames into the reply beyond one look you needed.

If the renderer prints errors, it lists all of them with JSON paths. Fix them all, then rerun.

## Getting good-looking results

- **Coordinates:** Z up, ground at z = 0, meters, characters face +X. Built-in meshes are 1 unit and centered; `"pivot": "bottom"` puts the origin at the base so z = 0 rests on the floor.
- **Camera:** the field of view is a wide 90° vertical. Get close: about 1× the subject's height away, a bit above, looking down 15–30° with `lookAt`. Orbits, slow push-ins and `follow` tracking shots look premium.
- **Light:** one directional `sun` from the side and above (e.g. `[5, -6, 7]` with `lookAt` the subject) gives readable shapes and a visible ground shadow. From behind the camera it flattens everything.
- **Palette:** 3–5 harmonious colors. For a seamless studio backdrop copy `studio.json`'s environment; for a diorama use a contrasting ground.
- **Shapes:** beyond cube/sphere/cylinder/cone: `roundbox` (soft edges), `torus`/`ring`, `gem`, `rock`, `lathe` (vases, cups, bottles from a profile), and `"flat": true` for a faceted low-poly look.
- **Motion:** ease everything (`inOutCubic`, `outBack` for pop-ins). Use the helpers instead of hand-keying: `bounce` (hops with squash), `path` (travel with `face`), `follow` (camera tracking), `walk` (leg/arm cycle), `"repeat"`/`"period"` for looping keys. Loops: full spin/orbit periods that divide the duration.
- **Characters:** start from `character.json`. Joints are groups (objects with no mesh); parts hang below them. Squash the root, not one part.
- **Clip length:** 3–8 s at 30 fps. Portrait 1080×1350 works for Instagram and LinkedIn feeds.

## Limits

- Shadows fall only on the ground (from the first directional light); objects don't shadow each other.
- No text in the render. If the user wants captions, add them to the MP4 afterwards with ffmpeg's `drawtext`.
- No skeletal animation; glTF models import as static meshes (animate them as whole objects or build rigs from parts).
- One camera, fixed 90° vertical field of view.
- Windows and Linux (x64) only. On Linux without a display the script uses `xvfb-run` and the software renderer; if it or the X11/GL libraries are missing, install them (Debian/Ubuntu: `sudo apt-get install -y xvfb libgl1-mesa-dri libgl1 libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6`). Linux builds need glibc 2.35+ (Ubuntu 22.04, Debian 12, Fedora 36 or newer).
