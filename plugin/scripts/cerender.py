#!/usr/bin/env python3
"""clubengine-render launcher: JSON scene in, PNG frames (and optionally MP4/GIF) out.

    python cerender.py render scene.json --preview                 # 6 labeled moments on a contact sheet
    python cerender.py render scene.py --preview                   # a script that prints the scene JSON (see cerender_lib.py)
    python cerender.py render scene.json --times 1.2,1.5,2         # just these moments (seconds)
    python cerender.py render scene.json --range 3.5-5 --step 0.1  # every 0.1 s from 3.5 to 5
    python cerender.py render scene.json --frame 42                # one full-quality frame
    python cerender.py render scene.json --mp4 [--gif] [--out clip.mp4] [--keep-frames]
    python cerender.py setup      # download the renderer (and ffmpeg if missing) and print where they are

Standard library only. The renderer is downloaded once per plugin version from the
GitHub release that matches plugin.json, into a per-user cache folder.
Set CERENDER_HOME to a local runtime folder (bin/, Config/, Content/, Engine/) to skip the download.
ffmpeg comes from --ffmpeg, CERENDER_FFMPEG, PATH, or a one-time download of a static LGPL build.
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
import tarfile
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path

REPO = "ShotaKhakhishvili/clubengine-render"
PLUGIN_ROOT = Path(__file__).resolve().parent.parent
MAX_TEXTURE = 4096
SHEET_FRAMES = 6
MAX_MOMENTS = 48

# Static LGPL builds (BtbN/FFmpeg-Builds), used when ffmpeg isn't installed
FFMPEG_BUILDS = {
    "windows-x64": "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl.zip",
    "linux-x64": "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linux64-lgpl.tar.xz",
}
FFMPEG_INSTALL = {
    "Windows": "winget install --id Gyan.FFmpeg -e",
    "Linux": "sudo apt-get install -y ffmpeg",
}


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
            download(url, archive, "the renderer")
        except RuntimeError as e:
            fail(f"{e}\n  If this machine can't reach GitHub, download that zip by hand and set CERENDER_HOME to the unpacked folder.")

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


def run_scene_script(script: Path) -> Path:
    """Runs a .py scene with this Python and returns a temp file holding the JSON it printed.
    cerender_lib (next to this file) is importable from the script; it runs in its own folder."""
    env = dict(os.environ)
    env["PYTHONPATH"] = str(Path(__file__).resolve().parent) + (os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
    env["PYTHONIOENCODING"] = "utf-8"
    result = subprocess.run([sys.executable, str(script)], cwd=script.parent, env=env,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        print(f"error: {script.name} failed (exit code {result.returncode}):", file=sys.stderr)
        print(result.stderr.rstrip() or "  (no error output)", file=sys.stderr)
        sys.exit(1)
    if result.stderr.strip():
        print(result.stderr.rstrip(), file=sys.stderr)  # the script's own messages

    text = result.stdout.strip()
    try:
        json.loads(text)
    except json.JSONDecodeError as e:
        start = text[:160] + ("..." if len(text) > 160 else "")
        fail(f"{script.name} must print only the scene JSON to stdout (print anything else to stderr): {e}\n"
             f"  it printed: {start!r}")

    handle, name = tempfile.mkstemp(prefix=f"{script.stem}-", suffix=".json")
    with os.fdopen(handle, "w", encoding="utf-8") as out:
        out.write(text)
    return Path(name)


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


def download(url: str, target: Path, what: str) -> None:
    try:
        with urllib.request.urlopen(url, timeout=120) as response, open(target, "wb") as out:
            shutil.copyfileobj(response, out)
    except Exception as e:  # noqa: BLE001 - any network error means the same thing here
        raise RuntimeError(f"downloading {what} failed ({e}). URL: {url}") from e


# ---------- ffmpeg ----------

def ffmpeg_exe_name() -> str:
    return "ffmpeg.exe" if platform.system() == "Windows" else "ffmpeg"


def download_ffmpeg() -> Path:
    """Fetches a static ffmpeg into the cache once and returns its path."""
    tag = platform_tag()
    target_dir = cache_root() / f"ffmpeg-{tag}"
    exe = target_dir / ffmpeg_exe_name()
    if exe.exists():
        return exe

    url = FFMPEG_BUILDS[tag]
    print("cerender: ffmpeg not found, downloading a static build (~100-150 MB, one time)...", file=sys.stderr)
    target_dir.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target_dir.parent) as tmp:
        archive = Path(tmp) / url.rsplit("/", 1)[-1]
        download(url, archive, "ffmpeg")

        # Only the ffmpeg executable (bin/ffmpeg[.exe]) is needed
        staged = Path(tmp) / ffmpeg_exe_name()
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as z:
                member = next((n for n in z.namelist() if n.endswith("/bin/" + ffmpeg_exe_name())), None)
                if not member:
                    raise RuntimeError("the ffmpeg archive has no bin/" + ffmpeg_exe_name())
                with z.open(member) as src, open(staged, "wb") as dst:
                    shutil.copyfileobj(src, dst)
        else:
            with tarfile.open(archive, "r:*") as t:
                member = next((m for m in t.getmembers() if m.isfile() and m.name.endswith("/bin/" + ffmpeg_exe_name())), None)
                if not member:
                    raise RuntimeError("the ffmpeg archive has no bin/" + ffmpeg_exe_name())
                with t.extractfile(member) as src, open(staged, "wb") as dst:
                    shutil.copyfileobj(src, dst)
        staged.chmod(staged.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        target_dir.mkdir(parents=True, exist_ok=True)
        shutil.move(str(staged), str(exe))
    return exe


def find_ffmpeg(explicit: str | None) -> Path:
    """--ffmpeg, then CERENDER_FFMPEG, then PATH, then the cached/downloaded build. Exits with an install hint if all fail."""
    for source, value in (("--ffmpeg", explicit), ("CERENDER_FFMPEG", os.environ.get("CERENDER_FFMPEG"))):
        if value:
            path = Path(value).expanduser()
            if path.is_dir():
                path = path / ffmpeg_exe_name()
            if not path.is_file():
                fail(f"{source} points to {path}, which isn't an ffmpeg executable")
            return path

    on_path = shutil.which("ffmpeg")
    if on_path:
        return Path(on_path)

    try:
        return download_ffmpeg()
    except Exception as e:  # noqa: BLE001
        install = FFMPEG_INSTALL.get(platform.system(), "install ffmpeg")
        fail(f"MP4/GIF need ffmpeg, which isn't installed and couldn't be downloaded ({e}).\n"
             f"  Install it with: {install}   (or pass --ffmpeg PATH), then run again.")
    return Path()


def pick_encoder(ffmpeg: Path) -> str:
    """Codec arguments for the best H.264 encoder that actually works in this ffmpeg (LGPL builds have openh264
    instead of x264), checked with a tiny test encode so a broken one fails before rendering, not after."""
    listing = subprocess.run([str(ffmpeg), "-hide_banner", "-encoders"], capture_output=True, text=True, errors="replace").stdout
    options = []
    if " libx264 " in listing:
        options.append("-c:v libx264 -preset veryfast -crf 18")
    if " libopenh264 " in listing:
        options.append("-c:v libopenh264 -b:v 12M")
    options.append("-c:v mpeg4 -q:v 2")

    with tempfile.TemporaryDirectory() as tmp:
        for option in options:
            test = Path(tmp) / "test.mp4"
            result = subprocess.run([str(ffmpeg), "-y", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=gray:s=64x64:r=30:d=0.2",
                                     *option.split(), "-pix_fmt", "yuv420p", str(test)], capture_output=True)
            if result.returncode == 0 and test.exists():
                return option
    fail(f"{ffmpeg} can't encode MP4 (no working H.264 or MPEG-4 encoder).\n"
         f"  Install a full ffmpeg with: {FFMPEG_INSTALL.get(platform.system(), 'your package manager')}, or pass --ffmpeg PATH.")
    return ""


def make_gif(ffmpeg: Path, mp4: Path, gif: Path, fps: int) -> bool:
    gif.parent.mkdir(parents=True, exist_ok=True)
    gif.unlink(missing_ok=True)
    palette = ("fps={0},scale=540:-1:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];"
               "[b][p]paletteuse=dither=bayer:bayer_scale=4").format(min(fps, 25))
    result = subprocess.run([str(ffmpeg), "-y", "-loglevel", "error", "-i", str(mp4), "-filter_complex", palette, str(gif)],
                            capture_output=True, text=True, errors="replace")
    if not gif.exists():
        print(f"error: ffmpeg couldn't make the GIF: {result.stderr.strip()[-400:]}", file=sys.stderr)
    return gif.exists()


# ---------- render ----------

def parse_size(text: str, width: int, height: int) -> tuple[int, int]:
    """"WxH", or one number for the width (height keeps the aspect ratio)."""
    try:
        if "x" in text.lower():
            w, h = (int(v) for v in text.lower().split("x"))
        else:
            w = int(text)
            h = round(height * w / width)
    except ValueError:
        fail(f"--size expects WxH or a width, got {text!r}")
    if not (16 <= w <= MAX_TEXTURE and 16 <= h <= MAX_TEXTURE):
        fail(f"--size must be between 16 and {MAX_TEXTURE} on each side")
    return w, h


def parse_moments(args: argparse.Namespace, duration: float) -> list[float]:
    """Times in seconds from --times or --range/--step."""
    try:
        if args.times:
            times = [float(t) for t in args.times.replace(" ", "").split(",") if t]
        else:
            start_text, _, end_text = args.range.replace(" ", "").replace(":", "-").partition("-")
            start, end = float(start_text), float(end_text or start_text)
            if end < start:
                start, end = end, start
            step = args.step if args.step else (end - start) / (SHEET_FRAMES - 1) or 1.0
            if step <= 0:
                fail("--step must be more than 0")
            count = int(round((end - start) / step + 1e-9)) + 1
            times = [round(start + i * step, 6) for i in range(count)]
    except ValueError:
        fail("--times expects seconds like 1.2,1.5,2 and --range like 3.5-5")
        return []
    if not times:
        fail("no times given")
    if len(times) > MAX_MOMENTS:
        fail(f"{len(times)} moments is too many for one sheet (max {MAX_MOMENTS}); use a bigger --step")
    bad = [t for t in times if t < 0 or (duration > 0 and t > duration + 1e-6)]
    if bad:
        fail(f"time {bad[0]} is outside the clip (0 to {duration} s)")
    return times


def run_renderer(home: Path, scene: Path, out_dir: Path, width: int, height: int, fps: int,
                 supersample: int, count: int, extra: list[str], source: Path) -> None:
    engine_out = out_dir / ".engine"
    command = linux_wrapper() + [
        str(home / "bin" / exe_name()),
        str(scene),
        "--out", str(out_dir),
        "--ss", str(supersample),
        "--size", f"{width}x{height}",
        f"-OfflineRender.Width={width * supersample}",
        f"-OfflineRender.Height={height * supersample}",
        f"-OfflineRender.FPS={fps}",
        f"-OfflineRender.StartFrame={count - 1}",
        f"-OfflineRender.EndFrame={count - 1}",
        f"-OfflineRender.OutputDir={engine_out}",
        *extra,
    ]

    env = dict(os.environ)
    if platform.system() == "Linux":
        env["LD_LIBRARY_PATH"] = str(home / "bin") + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        # Mesa's software renderer reports 4.5 but runs the engine's 4.6 shaders fine
        env.setdefault("MESA_GL_VERSION_OVERRIDE", "4.6")
        env.setdefault("MESA_GLSL_VERSION_OVERRIDE", "460")

    result = subprocess.run(command, cwd=home, env=env, capture_output=True, text=True, errors="replace")
    shutil.rmtree(engine_out, ignore_errors=True)

    lines = (result.stdout + result.stderr).splitlines()
    ours = [line for line in lines if line.startswith("CERENDER") or line.startswith("  - ")]
    for line in ours:
        if source != scene:
            line = line.replace(str(scene), f"the JSON printed by {source.name}")
        print(line.replace("CERENDER ERROR: ", "error: ").replace("CERENDER: ", ""))

    if result.returncode != 0:
        if ours and "--video" in extra:
            # ffmpeg shares the renderer's stderr: its message is whatever isn't ours or an engine log line
            for line in [l for l in lines if l not in ours and not l.startswith("\x1b[") and "CLUBENGINE_ROOT" not in l and l.strip()][-6:]:
                print("  ffmpeg: " + line, file=sys.stderr)
        if not ours:
            print("error: the renderer failed. Last log lines:", file=sys.stderr)
            if platform.system() == "Linux":
                print("  (on a bare Linux machine this is usually missing system libraries: "
                      "sudo apt-get install -y xvfb libgl1-mesa-dri libgl1 libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6)", file=sys.stderr)
            for line in lines[-25:]:
                print("  " + line, file=sys.stderr)
        sys.exit(result.returncode or 1)


def render(args: argparse.Namespace) -> int:
    source = Path(args.scene).expanduser().resolve()
    if not source.exists():
        fail(f"scene file not found: {source}")
    if source.suffix.lower() != ".py":
        return render_scene(args, source, source)

    # A .py scene prints its JSON; it's rendered from a temp file, so nothing big lands in the user's folder
    scene = run_scene_script(source)
    try:
        return render_scene(args, source, scene)
    finally:
        scene.unlink(missing_ok=True)


def render_scene(args: argparse.Namespace, source: Path, scene: Path) -> int:
    """source is what the user gave (outputs are named after it), scene the JSON file to render."""
    # --out is the frames folder, or the video file when it ends in .mp4/.gif
    video_out = None
    out_dir = source.parent / "renders" / source.stem
    if args.out:
        target = Path(args.out).expanduser().resolve()
        if target.suffix.lower() in (".mp4", ".gif"):
            video_out = target
            args.mp4 = args.mp4 or target.suffix.lower() == ".mp4"
            args.gif = args.gif or target.suffix.lower() == ".gif"
        else:
            out_dir = target
    out_dir.mkdir(parents=True, exist_ok=True)

    output = read_output_settings(scene)
    width = int(number(output.get("width"), 1080))
    height = int(number(output.get("height"), 1080))
    fps = int(number(output.get("fps"), 30))
    duration = float(number(output.get("duration"), 0))
    frames = max(1, round(duration * fps)) if duration > 0 else 1

    moments = bool(args.times or args.range)
    picks = [args.preview, moments, args.frame is not None]
    if sum(picks) > 1:
        fail("use only one of --preview, --times/--range and --frame")
    if args.step and not args.range:
        fail("--step goes with --range")
    if (args.mp4 or args.gif) and (args.preview or moments or args.frame is not None):
        fail("--mp4/--gif make a video of the whole clip; leave out --preview, --times, --range and --frame")

    want_video = (args.mp4 or args.gif) and frames > 1
    if args.keep_frames and not (args.mp4 or args.gif):
        fail("--keep-frames goes with --mp4/--gif (without them every frame is a PNG anyway)")
    if (args.mp4 or args.gif) and frames == 1:
        print("note: this scene is a still image (no output.duration), so there's no MP4/GIF to make")

    # Check for ffmpeg before spending time on frames
    ffmpeg = find_ffmpeg(args.ffmpeg) if want_video else None
    codec = pick_encoder(ffmpeg) if ffmpeg else ""

    # Moments and previews: half size, no supersampling, labeled contact sheet only
    quick = args.preview or moments
    if args.size:
        width, height = parse_size(args.size, width, height)
    elif quick:
        width, height = max(16, width // 2), max(16, height // 2)
    supersample = 1 if quick else (2 if max(width, height) * 2 <= MAX_TEXTURE else 1)

    if moments:
        times = parse_moments(args, duration)
    elif args.preview and frames > 1:
        count = min(SHEET_FRAMES, frames)
        times = [round(i * (frames - 1) / (count - 1)) / fps for i in range(count)]
    elif args.frame is not None:
        if not 0 <= args.frame < frames:
            fail(f"--frame must be from 0 to {frames - 1}")
        times = [args.frame / fps]
    else:
        times = []

    home = runtime_dir()
    extra = ["--base-dir", str(source.parent)]
    if times:
        extra += ["--times", ",".join(f"{t:.6g}" for t in times)]
    sheet = bool(times) and quick
    if sheet:
        extra.append("--sheet-only")

    # Final videos: CERender streams each frame straight into ffmpeg. A GIF is made from the MP4 afterwards.
    mp4 = gif = None
    if want_video:
        if args.gif:
            gif = video_out.with_suffix(".gif") if video_out else out_dir / f"{source.stem}.gif"
        if args.mp4:
            mp4 = video_out.with_suffix(".mp4") if video_out else out_dir / f"{source.stem}.mp4"
        encode_to = mp4 or out_dir / f".{source.stem}.tmp.mp4"
        extra += ["--video", str(encode_to), "--ffmpeg", str(ffmpeg), "--vcodec", codec]
        if not args.keep_frames:
            extra.append("--no-frames")

    # A full render replaces the frames of an earlier one; a sheet or single frame leaves them alone
    if not times:
        for old in out_dir.glob("frame_*.png"):
            old.unlink()
    if sheet or not times:
        (out_dir / "contact_sheet.png").unlink(missing_ok=True)

    started = time.time()
    run_renderer(home, scene, out_dir, width, height, fps, supersample, len(times) or frames, extra, source)
    elapsed = time.time() - started

    quality = " (2x supersampled)" if supersample > 1 else ""
    sheet_path = out_dir / "contact_sheet.png"
    if sheet:
        print(f"contact sheet: {sheet_path}")
        print(f"  {len(times)} moment(s) at " + ", ".join(f"{t:.2f}s" for t in times)
              + f" (each tile is labeled), rendered at {width}x{height} in {elapsed:.1f}s")
    elif args.frame is not None or frames == 1:
        number_ = args.frame if args.frame is not None else 0
        print(f"image: {out_dir / f'frame_{number_:04d}.png'}")
        print(f"  {width}x{height}{quality} in {elapsed:.1f}s")
    else:
        written = "--no-frames" not in extra
        print(f"rendered {frames} frames at {width}x{height}{quality} in {elapsed:.1f}s")
        if written:
            print(f"frames: {out_dir / 'frame_%04d.png'}")
        if sheet_path.exists():
            print(f"contact sheet: {sheet_path}")

    if want_video:
        if mp4:
            print(f"mp4: {mp4}")
        if gif:
            ok = make_gif(ffmpeg, encode_to, gif, fps)
            if not mp4:
                encode_to.unlink(missing_ok=True)
            if not ok:
                return 1
            print(f"gif: {gif}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(prog="cerender", description="Render ClubEngine JSON scenes.")
    sub = parser.add_subparsers(dest="command", required=True)

    r = sub.add_parser("render", help="render a scene file")
    r.add_argument("scene", help="path to the scene .json, or a .py script that prints it")
    r.add_argument("--out", help="frames folder (default: renders/<scene name>/ next to the scene), "
                                 "or the video file if it ends in .mp4/.gif")
    r.add_argument("--preview", action="store_true", help="fast check: 6 labeled moments on a contact sheet, half size")
    r.add_argument("--times", help="only these moments, in seconds, e.g. 3.5,3.95,5.5 (labeled contact sheet)")
    r.add_argument("--range", help="moments from A to B seconds, e.g. 3.5-5 (with --step)")
    r.add_argument("--step", type=float, help="seconds between --range moments (default: 6 moments)")
    r.add_argument("--frame", type=int, help="render only this frame number, full quality, as one PNG")
    r.add_argument("--size", help="output size WxH, or a width (keeps the aspect ratio)")
    r.add_argument("--mp4", action="store_true", help="also encode an MP4 (finds or downloads ffmpeg first)")
    r.add_argument("--gif", action="store_true", help="also encode a GIF (finds or downloads ffmpeg first)")
    r.add_argument("--ffmpeg", help="path to ffmpeg (default: CERENDER_FFMPEG, PATH, or a one-time download)")
    r.add_argument("--keep-frames", action="store_true", help="with --mp4/--gif, also keep the PNG frames")

    s = sub.add_parser("setup", help="download the renderer and ffmpeg if missing, and print where they are")
    s.add_argument("--ffmpeg", help="path to ffmpeg to check instead")

    args = parser.parse_args()
    if args.command == "setup":
        print(f"renderer: {runtime_dir()}")
        print(f"ffmpeg:   {find_ffmpeg(args.ffmpeg)}")
        return 0
    return render(args)


if __name__ == "__main__":
    sys.exit(main())
