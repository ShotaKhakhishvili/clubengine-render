# cerender.py: the launcher

The flag list is in SKILL.md's cheat sheet. This file has the details.

```bash
python "${CLAUDE_PLUGIN_ROOT}/scripts/cerender.py" render SCENE [flags]
python "${CLAUDE_PLUGIN_ROOT}/scripts/cerender.py" setup [--ffmpeg PATH]
```

Use `python3` if `python` isn't found. The first run downloads the renderer (~3 MB) once into a per-user cache (`CERENDER_HOME` overrides where it's looked up). `setup` downloads the renderer and ffmpeg if they're missing and prints where they are.

## Render modes

Use only one of `--preview`, `--times`/`--range` and `--frame`. `--mp4`/`--gif` always render the whole clip, so they don't combine with any of them.

| mode | size | output |
|---|---|---|
| no flag | full, 2× supersampled | every frame as `frame_NNNN.png` (a still: one image) plus a contact sheet |
| `--preview` | half, no supersampling | `contact_sheet.png` with 6 moments spread over the clip, each tile labeled with its time |
| `--times 3.5,3.95,5.5` | half, no supersampling | contact sheet of exactly those moments (seconds, 0 to duration) |
| `--range 3.5-5 [--step 0.1]` | half, no supersampling | contact sheet from A to B every `step` seconds (default: 6 moments). At most 48 moments per sheet |
| `--frame N` | full, 2× supersampled | one PNG, `frame_NNNN.png`. N runs from 0 to duration × fps − 1 |
| `--mp4` / `--gif` | full, 2× supersampled | the whole clip streamed into ffmpeg; the contact sheet is still written |

- `--size 960` (a width, keeps the aspect ratio) or `--size 960x540` sets the output size for any mode. With `--frame` it stays 2× supersampled (as long as twice the size fits in 4096 px).
- Contact sheet tiles are small on purpose: at most 240 px on their long side for up to 6 moments, 200 px up to 16, 160 px beyond. Judge layout on a sheet, details on `--frame`.
- A full render replaces the PNG frames of an earlier one; a sheet or a single frame leaves them alone.

## Output paths

- Default folder: `renders/<scene name>/` next to the scene file.
- `--out folder` puts frames and sheets there; `--out path/clip.mp4` (or `.gif`) names the video and implies `--mp4` (`--gif`).
- `--mp4` streams raw frames into ffmpeg: no PNG frames are written unless you add `--keep-frames`. A GIF is made from that MP4 afterwards.
- It prints the paths it wrote: `contact sheet:`, `image:`, `frames:`, `mp4:`, `gif:`. Report those to the user.

## ffmpeg

ffmpeg is found or fetched **before** rendering: `--ffmpeg PATH`, then `CERENDER_FFMPEG`, then `PATH`, else a one-time download of a static LGPL build into the renderer's cache. If that fails the script stops with a one-line install command: give it to the user; don't try to install ffmpeg some other way.

## .py scenes

`render scene.py` runs the script with the same Python, in the script's folder, with `cerender_lib` on `PYTHONPATH`, and renders what it printed from a temp file. A failing script shows its traceback; anything it prints to stderr is passed through. See python-lib.md.

## Platforms

Windows and Linux (x64) only. On Linux without a display the script uses `xvfb-run` and the software renderer; if it or the X11/GL libraries are missing, install them (Debian/Ubuntu: `sudo apt-get install -y xvfb libgl1-mesa-dri libgl1 libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6`). Linux builds need glibc 2.35+ (Ubuntu 22.04, Debian 12, Fedora 36 or newer).

## Errors

If the renderer prints errors, it lists all of them with JSON paths. Fix them all, then rerun. For a `.py` scene the paths point into the printed JSON (`objects[12].emitter.count`): find the code that made that object and fix the script; don't open the generated JSON.
