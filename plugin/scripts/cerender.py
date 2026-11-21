#!/usr/bin/env python3
"""clubengine-render launcher: JSON scene in, PNG frames (and optionally MP4/GIF) out.

    python cerender.py render scene.json [--preview] [--mp4] [--gif] [--out DIR]
    python cerender.py setup      # download the renderer for this OS and print where it is

Standard library only. The renderer is downloaded once per plugin version from the
GitHub release that matches plugin.json, into a per-user cache folder.
Set CERENDER_HOME to a local runtime folder (bin/, Config/, Content/, Engine/) to skip the download.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path

REPO = "ShotaKhakhishvili/clubengine-render"
PLUGIN_ROOT = Path(__file__).resolve().parent.parent
MAX_TEXTURE = 4096


def fail(message: str, code: int = 1) -> None:
    print(f"cerender: {message}", file=sys.stderr)
    sys.exit(code)


def plugin_version() -> str:
    manifest = PLUGIN_ROOT / ".claude-plugin" / "plugin.json"
    return json.loads(manifest.read_text(encoding="utf-8"))["version"]


def platform_tag() -> str:
    system = platform.system()
    machine = platform.machine().lower()
    if machine not in ("x86_64", "amd64"):
        fail(f"no build for {system} {machine} yet (only x86_64 Windows and Linux)")
    if system == "Windows":
        return "windows-x64"
    if system == "Linux":
        return "linux-x64"
    if system == "Darwin":
        fail("macOS isn't supported yet: the engine needs OpenGL 4.6 and macOS stops at 4.1. "
             "It's planned once the engine's Vulkan backend lands.")
    fail(f"unsupported OS: {system}")
    return ""


def cache_root() -> Path:
    if platform.system() == "Windows":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    else:
        base = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    return base / "clubengine-render"


def exe_name() -> str:
    return "CERender.exe" if platform.system() == "Windows" else "CERender"


def runtime_dir() -> Path:
    """Folder with bin/CERender, downloading it first if needed."""
    override = os.environ.get("CERENDER_HOME")
    if override:
        home = Path(override).expanduser().resolve()
        if not (home / "bin" / exe_name()).exists():
            fail(f"CERENDER_HOME={home} has no bin/{exe_name()}")
        return home

    version = plugin_version()
    tag = platform_tag()
    home = cache_root() / f"{version}-{tag}"
    if (home / "bin" / exe_name()).exists():
        return home

    url = f"https://github.com/{REPO}/releases/download/v{version}/clubengine-render-{tag}.zip"
    print(f"cerender: downloading the renderer v{version} for {tag} (one time)...", file=sys.stderr)

    home.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=home.parent) as tmp:
        archive = Path(tmp) / "runtime.zip"
        try:
            with urllib.request.urlopen(url, timeout=120) as response, open(archive, "wb") as out:
                shutil.copyfileobj(response, out)
        except Exception as e:  # noqa: BLE001 - any network error means the same thing here
            fail(f"download failed ({e}).\n  URL: {url}\n"
                 "  If this machine can't reach GitHub, download that zip by hand and set CERENDER_HOME to the unpacked folder.")

        staging = Path(tmp) / "unpacked"
        with zipfile.ZipFile(archive) as z:
            z.extractall(staging)

        # The zip holds one top-level folder
        entries = list(staging.iterdir())
        root = entries[0] if len(entries) == 1 and entries[0].is_dir() else staging

        # zipfile drops the executable bit
        exe = root / "bin" / exe_name()
        exe.chmod(exe.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        if home.exists():
            shutil.rmtree(home)
        shutil.move(str(root), str(home))

    return home


def linux_wrapper() -> list[str]:
    """Without a display, run under a virtual X server. Mesa's software renderer covers machines without a GPU."""
    if platform.system() != "Linux" or os.environ.get("DISPLAY"):
        return []
    xvfb = shutil.which("xvfb-run")
    if not xvfb:
        fail("no display found and xvfb-run isn't installed.\n"
             "  Install it (Debian/Ubuntu: sudo apt-get install -y xvfb libgl1-mesa-dri libgl1 libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6) and run again.")
    return [xvfb, "-a", "-s", "-screen 0 1280x720x24"]


def strip_comments(text: str) -> str:
    """Removes // and /* */ comments outside strings (the renderer accepts them, Python's json doesn't)."""
    out, i, n, in_string = [], 0, len(text), False
    while i < n:
        c = text[i]
        if in_string:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 1
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
            out.append(c)
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        else:
            out.append(c)
        i += 1
    return "".join(out)


def read_output_settings(scene: Path) -> dict:
    try:
        data = json.loads(strip_comments(scene.read_text(encoding="utf-8")))
    except json.JSONDecodeError:
        return {}  # the renderer reports the exact JSON error
    out = data.get("output", {}) if isinstance(data, dict) else {}
    return out if isinstance(out, dict) else {}


def number(value, default):
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) else default


def render(args: argparse.Namespace) -> int:
    scene = Path(args.scene).expanduser().resolve()
    if not scene.exists():
        fail(f"scene file not found: {scene}")

    out_dir = Path(args.out).expanduser().resolve() if args.out else scene.parent / "renders" / scene.stem
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in list(out_dir.glob("frame_*.png")) + list(out_dir.glob("contact_sheet.png")):
        old.unlink()

    output = read_output_settings(scene)
    width = int(number(output.get("width"), 1080))
    height = int(number(output.get("height"), 1080))
    fps = int(number(output.get("fps"), 30))
    duration = float(number(output.get("duration"), 0))
    frames = max(1, round(duration * fps)) if duration > 0 else 1

    if args.preview:
        width, height = max(16, width // 2), max(16, height // 2)
        supersample = 1
    else:
        supersample = 2 if max(width, height) * 2 <= MAX_TEXTURE else 1

    home = runtime_dir()
    engine_out = out_dir / ".engine"

    command = linux_wrapper() + [
        str(home / "bin" / exe_name()),
        str(scene),
        "--out", str(out_dir),
        "--ss", str(supersample),
        f"-OfflineRender.Width={width * supersample}",
        f"-OfflineRender.Height={height * supersample}",
        f"-OfflineRender.FPS={fps}",
        f"-OfflineRender.StartFrame={frames - 1}",
        f"-OfflineRender.EndFrame={frames - 1}",
        f"-OfflineRender.OutputDir={engine_out}",
    ]
    if args.preview:
        command.append("--sheet-only")
        # The scene file's own size is what the app expects; tell it the preview size instead
        command += ["--size", f"{width}x{height}"]

    env = dict(os.environ)
    if platform.system() == "Linux":
        env["LD_LIBRARY_PATH"] = str(home / "bin") + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        # Mesa's software renderer reports 4.5 but runs the engine's 4.6 shaders fine
        env.setdefault("MESA_GL_VERSION_OVERRIDE", "4.6")
        env.setdefault("MESA_GLSL_VERSION_OVERRIDE", "460")

    started = time.time()
    result = subprocess.run(command, cwd=home, env=env, capture_output=True, text=True, errors="replace")
    shutil.rmtree(engine_out, ignore_errors=True)

    lines = (result.stdout + result.stderr).splitlines()
    ours = [l for l in lines if l.startswith("CERENDER") or l.startswith("  - ")]
    for line in ours:
        print(line.replace("CERENDER: ", "").replace("CERENDER ERROR: ", "error: "))

    if result.returncode != 0:
        if not ours:
            print("error: the renderer failed. Last log lines:", file=sys.stderr)
            if platform.system() == "Linux":
                print("  (on a bare Linux machine this is usually missing system libraries: "
                      "sudo apt-get install -y xvfb libgl1-mesa-dri libgl1 libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6)", file=sys.stderr)
            for line in lines[-25:]:
                print("  " + line, file=sys.stderr)
        return result.returncode or 1

    elapsed = time.time() - started
    print(f"rendered {frames} frame(s) at {width}x{height}"
          f"{' (2x supersampled)' if supersample > 1 else ''}{' [preview]' if args.preview else ''} in {elapsed:.1f}s")
    print(f"output: {out_dir}")

    if not args.preview and frames > 1 and (args.mp4 or args.gif):
        encode(out_dir, scene.stem, fps, args.mp4, args.gif)
    return 0


def encode(out_dir: Path, name: str, fps: int, mp4: bool, gif: bool) -> None:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("note: ffmpeg isn't installed, so no MP4/GIF was made. The PNG frames are in the output folder.")
        return

    frames = str(out_dir / "frame_%04d.png")
    if mp4:
        target = out_dir / f"{name}.mp4"
        subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(fps), "-i", frames,
                        "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", "-c:v", "libx264", "-preset", "veryfast",
                        "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(target)], check=False)
        if target.exists():
            print(f"video: {target}")
    if gif:
        target = out_dir / f"{name}.gif"
        palette = "fps={0},scale=540:-1:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4".format(min(fps, 25))
        subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(fps), "-i", frames,
                        "-filter_complex", palette, str(target)], check=False)
        if target.exists():
            print(f"gif: {target}")


def main() -> int:
    parser = argparse.ArgumentParser(prog="cerender", description="Render ClubEngine JSON scenes.")
    sub = parser.add_subparsers(dest="command", required=True)

    r = sub.add_parser("render", help="render a scene file")
    r.add_argument("scene", help="path to the scene .json")
    r.add_argument("--out", help="output folder (default: renders/<scene name>/ next to the scene)")
    r.add_argument("--preview", action="store_true", help="fast check: half size, no supersampling, contact sheet only")
    r.add_argument("--mp4", action="store_true", help="also encode an MP4 (needs ffmpeg)")
    r.add_argument("--gif", action="store_true", help="also encode a GIF (needs ffmpeg)")

    sub.add_parser("setup", help="download the renderer and print its folder")

    args = parser.parse_args()
    if args.command == "setup":
        print(runtime_dir())
        return 0
    return render(args)


if __name__ == "__main__":
    sys.exit(main())
