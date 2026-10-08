#!/usr/bin/env python3
"""Fetch CleriaCore's third-party dependencies into third_party/ (gitignored).

The pinned versions are the ones the Windows build has always used (issue #46):

  SDL3          3.4.12      Windows: SDL3-devel-3.4.12-VC.zip; Linux / macOS: built from the source
                            tarball and installed into third_party/sdl3 (needs cmake + a C compiler and,
                            on Linux, the X11 / Wayland / audio dev packages: see README.md)
  wgpu-native   v29.0.1.1   the release zip for this OS / CPU (include/, lib/, wgpu-native-meta/)
  Dear ImGui    v1.92.9b    third_party/imgui
  Lua           5.4.9       third_party/lua
  miniz         3.0.2       third_party/miniz
  stb_vorbis    v1.22       third_party/stb (nothings/stb, pinned commit)
  Fira Sans     (OFL)       third_party/fonts (google/fonts, pinned commit)

A component whose folder already exists is left alone (pass --force to replace it), so running this in
a working checkout never touches what is there.

  python tools/fetch_deps.py [--dest third_party] [--only sdl3,wgpu,...] [--force] [--jobs N]
"""
import argparse
import io
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

SDL_VERSION = "3.4.12"
WGPU_TAG = "v29.0.1.1"
IMGUI_TAG = "v1.92.9b"
LUA_VERSION = "5.4.9"
MINIZ_VERSION = "3.0.2"
STB_COMMIT = "2c980bb59875b0d32144a71867fbdebb2f77cd20"
FONTS_COMMIT = "5e8a3ba899557829a76cfdac30fa512bda91d7ca"
FONTS = ["firasans/FiraSans-Regular.ttf", "firasans/FiraSans-Medium.ttf", "firasans/FiraSans-SemiBold.ttf",
         "firasanscondensed/FiraSansCondensed-Medium.ttf", "firasanscondensed/FiraSansCondensed-SemiBold.ttf",
         "firasans/OFL.txt"]

COMPONENTS = ["sdl3", "wgpu", "imgui", "lua", "miniz", "stb", "fonts"]


def get(url):
    print(f"  GET {url}", flush=True)
    req = urllib.request.Request(url, headers={"User-Agent": "cleriacore-fetch-deps"})
    with urllib.request.urlopen(req, timeout=300) as r:
        return r.read()


def strip_top(names):
    """The single top-level folder every member shares, or '' (GitHub's pax_global_header ignored)."""
    names = [n for n in names if n and n != "pax_global_header"]
    tops = {n.split("/", 1)[0] for n in names}
    if len(tops) == 1 and any("/" in n.rstrip("/") for n in names):
        return tops.pop() + "/"
    return ""


def unzip(data, dest, strip=True):
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        names = z.namelist()
        top = strip_top(names) if strip else ""
        for info in z.infolist():
            rel = info.filename[len(top):] if top and info.filename.startswith(top) else info.filename
            if not rel or rel.endswith("/"):
                continue
            out = os.path.join(dest, rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(info) as src, open(out, "wb") as dst:
                shutil.copyfileobj(src, dst)
            mode = info.external_attr >> 16
            if mode & 0o111:
                os.chmod(out, 0o755)


def untar(data, dest):
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as t:
        members = t.getmembers()
        top = strip_top([m.name for m in members])
        for m in members:
            if not m.name.startswith(top):
                continue
            m.name = m.name[len(top):]
            if not m.name:
                continue
            if hasattr(tarfile, "data_filter"):
                t.extract(m, dest, filter="data")
            else:
                t.extract(m, dest)


def os_tag():
    s = sys.platform
    if s.startswith("win"):
        return "windows"
    if s == "darwin":
        return "macos"
    return "linux"


def cpu_tag():
    m = platform.machine().lower()
    return "aarch64" if m in ("arm64", "aarch64") else "x86_64"


def fetch_sdl3(dest, jobs):
    if os_tag() == "windows":
        unzip(get(f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/"
                  f"SDL3-devel-{SDL_VERSION}-VC.zip"), dest)
        return
    # Linux / macOS: build the release source and install it as a CMake package prefix.
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "src")
        untar(get(f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/"
                  f"SDL3-{SDL_VERSION}.tar.gz"), src)
        bld = os.path.join(tmp, "build")
        cfg = ["cmake", "-S", src, "-B", bld, "-DCMAKE_BUILD_TYPE=Release", "-DSDL_TESTS=OFF",
               "-DSDL_EXAMPLES=OFF", "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF",
               f"-DCMAKE_INSTALL_PREFIX={os.path.abspath(dest)}"]
        if os_tag() == "macos":
            cfg.append("-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0")
        subprocess.check_call(cfg)
        subprocess.check_call(["cmake", "--build", bld, "--parallel", str(jobs)])
        subprocess.check_call(["cmake", "--install", bld])


def fetch_wgpu(dest, jobs):
    o = os_tag()
    name = f"wgpu-{o}-{cpu_tag()}-msvc-release.zip" if o == "windows" else f"wgpu-{o}-{cpu_tag()}-release.zip"
    unzip(get(f"https://github.com/gfx-rs/wgpu-native/releases/download/{WGPU_TAG}/{name}"), dest, strip=False)


def fetch_imgui(dest, jobs):
    untar(get(f"https://github.com/ocornut/imgui/archive/refs/tags/{IMGUI_TAG}.tar.gz"), dest)


def fetch_lua(dest, jobs):
    untar(get(f"https://www.lua.org/ftp/lua-{LUA_VERSION}.tar.gz"), dest)


def fetch_miniz(dest, jobs):
    unzip(get(f"https://github.com/richgel999/miniz/releases/download/{MINIZ_VERSION}/miniz-{MINIZ_VERSION}.zip"),
          dest, strip=False)


def fetch_stb(dest, jobs):
    os.makedirs(dest, exist_ok=True)
    data = get(f"https://raw.githubusercontent.com/nothings/stb/{STB_COMMIT}/stb_vorbis.c")
    with open(os.path.join(dest, "stb_vorbis.c"), "wb") as f:
        f.write(data)


def fetch_fonts(dest, jobs):
    os.makedirs(dest, exist_ok=True)
    for f in FONTS:
        data = get(f"https://raw.githubusercontent.com/google/fonts/{FONTS_COMMIT}/ofl/{f}")
        with open(os.path.join(dest, os.path.basename(f)), "wb") as out:
            out.write(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dest", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "third_party"))
    ap.add_argument("--only", default=",".join(COMPONENTS), help="comma-separated: " + ",".join(COMPONENTS))
    ap.add_argument("--force", action="store_true", help="replace components that are already there")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    a = ap.parse_args()
    want = [c.strip() for c in a.only.split(",") if c.strip()]
    for c in want:
        if c not in COMPONENTS:
            sys.exit(f"unknown component {c!r}")
    root = os.path.abspath(a.dest)
    os.makedirs(root, exist_ok=True)
    for c in want:
        dest = os.path.join(root, c)
        if os.path.exists(dest):
            if not a.force:
                print(f"{c}: {dest} exists, kept (--force replaces it)")
                continue
            if os.path.islink(dest) or (hasattr(os.path, "isjunction") and os.path.isjunction(dest)):
                sys.exit(f"{c}: {dest} is a link / junction; remove it yourself")
            shutil.rmtree(dest)
        print(f"{c}: fetching into {dest}", flush=True)
        globals()["fetch_" + c](dest, a.jobs)
    print("done")


if __name__ == "__main__":
    main()
