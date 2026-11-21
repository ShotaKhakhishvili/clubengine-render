---
name: render-scene
description: Render 3D images and short animated clips from a JSON scene with the ClubEngine renderer. Use when the user wants a 3D render, a looping 3D animation, a product turntable, or a small animated 3D story (Instagram/LinkedIn-style clips), built from simple shapes or glTF models.
---

# Render scenes with ClubEngine

You describe a scene in one JSON file; a real engine renders it to PNG frames (and MP4/GIF if ffmpeg exists). You look at the result and iterate.

**Read `${CLAUDE_PLUGIN_ROOT}/skills/render-scene/reference.md` before writing your first scene.** It has every key. Working examples are in `${CLAUDE_PLUGIN_ROOT}/skills/render-scene/examples/`; start from the closest one.

## Workflow

1. **Write the scene** to a `.json` file in the user's working folder.
2. **Preview** (fast, half size, contact sheet of 6 frames):
   ```bash
   python "${CLAUDE_PLUGIN_ROOT}/scripts/cerender.py" render scene.json --preview
   ```
   Use `python3` if `python` isn't found. The first run downloads the renderer (~3 MB) once.
3. **Look at it.** Read `renders/<scene name>/contact_sheet.png` (or `frame_0000.png` for a still). Check framing, colors, motion, overlaps, things floating or sinking into the ground. Fix and preview again until it looks right; usually 1–3 rounds.
4. **Final render**:
   ```bash
   python "${CLAUDE_PLUGIN_ROOT}/scripts/cerender.py" render scene.json --mp4
   ```
   Add `--gif` for a GIF. Final renders are 2× supersampled (smooth edges).
5. **Report** the output folder and files. Don't paste frames into the reply beyond one look you needed.

If the renderer prints errors, it lists all of them with JSON paths. Fix them all, then rerun.

## Getting good-looking results

- **Coordinates:** Z up, ground at z = 0, meters. Built-in meshes are 1 unit and centered, so resting objects sit at z = half their height.
- **Camera:** the field of view is a wide 90° vertical. Get close: about 1× the subject's height away. Put it a bit above the subject looking down 15–30° with `lookAt`. Orbits and slow push-ins look premium.
- **Light:** one directional `sun` from the side and above (e.g. position `[5, -6, 7]` with `lookAt` the subject) gives readable shapes and a ground shadow the camera can see. Coming from behind the camera flattens everything and hides the shadows behind the objects. Add a point light for a warm accent or glow scenes.
- **Palette:** pick 3–5 harmonious colors (pastels on a pastel sky are the Instagram look). Match the ground color to the sky's bottom color for a seamless studio backdrop, or use a contrasting ground for a diorama look.
- **Motion:** ease everything (`inOutCubic`, `outBack` for pop-ins, `outBounce` for landings). Squash and stretch with `scale` keys sells bounces. Loops: make the last key equal the first and use full orbit/spin periods that divide the duration.
- **Characters and props:** build them from primitives parented to a root group (body, head, eyes, arms), then animate the root to move the whole thing and the parts for gestures. Squash and stretch the root (it scales from its origin, so put the origin at the feet), not one part, or parts poke through each other. Keep proportions chunky and cute.
- **Clip length:** 3–8 s at 30 fps. Portrait 1080×1350 works for both Instagram and LinkedIn feeds.

## Limits (version 1)

- Shadows fall only on the ground (from the first directional light); objects don't shadow each other.
- No text in the render. If the user wants captions and ffmpeg is installed, add them to the MP4 afterwards with ffmpeg's `drawtext`.
- No skeletal animation; glTF models import as static meshes (animate them as whole objects or build rigs from parts).
- One camera, fixed 90° vertical field of view.
- Windows and Linux (x64) only. On Linux without a display the script uses `xvfb-run` and the software renderer; if it or the X11/GL libraries are missing, install them (Debian/Ubuntu: `sudo apt-get install -y xvfb libgl1-mesa-dri libgl1 libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6`). Linux builds need glibc 2.35+ (Ubuntu 22.04, Debian 12, Fedora 36 or newer).
