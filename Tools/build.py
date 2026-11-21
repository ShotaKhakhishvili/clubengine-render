#!/usr/bin/env python3
"""Builds the engine and the render project, then packs a portable runtime zip.

    python Tools/build.py                  # build + package into dist/
    python Tools/build.py --bundle-runtime # also copy the C++ runtime libraries (used for releases)

Windows: run from an MSYS2 UCRT64 shell (gcc, cmake, ninja, python installed).
Linux:   needs gcc 13+, cmake, ninja and the X11/GL dev packages (see README).

Afterwards, render with the local build:
    CERENDER_HOME=dist/clubengine-render-<os>-x64 python plugin/scripts/cerender.py render plugin/skills/render-scene/examples/hello.json
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = ROOT / "External" / "KIU_ClubEngine" / "ClubEngine"
PROJECT = ROOT / "Project"
BUILD = ROOT / "Build"
DIST = ROOT / "dist"

IS_WINDOWS = platform.system() == "Windows"
TAG = "windows-x64" if IS_WINDOWS else "linux-x64"


def run(command: list[str], **kwargs) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, check=True, **kwargs)


def generator() -> list[str]:
    if shutil.which("ninja"):
        return ["-G", "Ninja"]
    return ["-G", "MinGW Makefiles"] if IS_WINDOWS else []


def engine_version() -> str:
    text = (ENGINE / "CMakeLists.txt").read_text(encoding="utf-8")
    return re.search(r"project\s*\(\s*ClubEngine\s+VERSION\s+([0-9.]+)", text).group(1)


def build_engine() -> Path:
    if not (ENGINE / "CMakeLists.txt").exists():
        sys.exit("Engine submodule is missing. Run: git submodule update --init")

    args = ["cmake", "-S", str(ENGINE), "-B", str(BUILD / "engine"), *generator(),
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_PROJECT_INCLUDE={ROOT / 'Tools' / 'cmake' / 'PortableEngineRoot.cmake'}"]
    if not IS_WINDOWS:
        args.append("-DGLFW_BUILD_WAYLAND=OFF")
    run(args)
    run(["cmake", "--build", str(BUILD / "engine"), "--parallel"])

    platform_name = "Win64" if IS_WINDOWS else "Linux64"
    return ENGINE / "Binaries" / platform_name / "Release" / engine_version()


def build_project(engine_bin: Path) -> None:
    run(["cmake", "-S", str(PROJECT), "-B", str(BUILD / "project"), *generator(),
         "-DCMAKE_BUILD_TYPE=Release", f"-DCE_BIN={engine_bin}"])
    run(["cmake", "--build", str(BUILD / "project"), "--parallel"])


def compiler_runtime_libs() -> list[Path]:
    """The C++ runtime the binaries were built against, so the zip runs on machines without it."""
    names = (["libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll"] if IS_WINDOWS
             else ["libstdc++.so.6", "libgcc_s.so.1"])
    found = []
    for name in names:
        compiler = os.environ.get("CXX", "g++")
        out = subprocess.run([compiler, f"-print-file-name={name}"], capture_output=True, text=True).stdout.strip()
        path = Path(out)
        if not path.is_absolute() and IS_WINDOWS:
            path = Path(shutil.which(compiler)).parent / name
        if path.is_file():
            found.append(path)
        else:
            print(f"warning: runtime library {name} not found", file=sys.stderr)
    return found


def package(engine_bin: Path, bundle_runtime: bool) -> Path:
    name = f"clubengine-render-{TAG}"
    out = DIST / name
    if out.exists():
        shutil.rmtree(out)
    (out / "bin").mkdir(parents=True)

    exe = "CERender.exe" if IS_WINDOWS else "CERender"
    shutil.copy2(PROJECT / "bin" / exe, out / "bin" / exe)

    pattern = "*.dll" if IS_WINDOWS else "*.so"
    for lib in engine_bin.glob(pattern):
        shutil.copy2(lib, out / "bin" / lib.name)

    if bundle_runtime:
        for lib in compiler_runtime_libs():
            shutil.copy2(lib.resolve(), out / "bin" / lib.name)  # keep the soname, copy the real file

    shutil.copytree(PROJECT / "Config", out / "Config")
    shutil.copytree(PROJECT / "Content", out / "Content")

    # Engine data the runtime reads, laid out as in the engine repo (paths are relative to the engine root)
    shutil.copytree(ENGINE / "Engine" / "Config", out / "Engine" / "Config")
    engine_public = Path("Engine/Source/Runtime/Engine/Public/Engine")
    for sub in ["Shaders/Common", "Geometry"]:
        shutil.copytree(ENGINE / engine_public / sub, out / engine_public / sub)

    shutil.copy2(ROOT / "LICENSE", out / "LICENSE")
    version = json.loads((ROOT / "plugin" / ".claude-plugin" / "plugin.json").read_text(encoding="utf-8"))["version"]
    (out / "VERSION").write_text(version + "\n", encoding="utf-8")

    archive = DIST / f"{name}.zip"
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for file in sorted(out.rglob("*")):
            if file.is_file():
                info = zipfile.ZipInfo.from_file(file, Path(name) / file.relative_to(out))
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, file.read_bytes())

    print(f"\npackaged: {out}\narchive:  {archive}")
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle-runtime", action="store_true", help="copy libstdc++/libgcc into the package")
    parser.add_argument("--skip-engine", action="store_true", help="reuse the last engine build")
    args = parser.parse_args()

    platform_name = "Win64" if IS_WINDOWS else "Linux64"
    engine_bin = (ENGINE / "Binaries" / platform_name / "Release" / engine_version()) if args.skip_engine else build_engine()
    build_project(engine_bin)
    package(engine_bin, args.bundle_runtime)
    return 0


if __name__ == "__main__":
    sys.exit(main())
