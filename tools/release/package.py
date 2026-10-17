#!/usr/bin/env python3
"""Package a Release build of CleriaCore for players (the release workflow, .github/workflows/release.yml).

    python tools/release/package.py --build build --out dist --version 0.1.0

Writes one archive into <out> for the platform it runs on:
  Windows  CleriaCore-<v>-windows-x64.zip     CleriaCore/ with the two programs, SDL3 + wgpu-native, the
                                              MSVC runtime DLLs (app-local, so no redistributable to install)
  Linux    CleriaCore-<v>-linux-x64.tar.gz    CleriaCore/ with the programs and lib/: every library the build
                                              itself provides (SDL3, wgpu-native, FFmpeg from
                                              tools/release/build_ffmpeg.sh); system libraries (glibc,
                                              libstdc++, OpenSSL, the GPU and audio stacks) stay the system's
  macOS    CleriaCore-<v>-macos-<arch>.zip    CleriaCore.app from tools/macos_bundle.py
Each holds FIRST-RUN.txt (tools/release/FIRST-RUN-<os>.txt) and the third-party licenses. None holds any
game data: a player's own Ys Origin install is unpacked on the first run.
"""
import argparse
import glob
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
TP = os.path.join(ROOT, "third_party")
LICENSES = {   # third_party file -> name in licenses/
    "sdl3/LICENSE.txt": "SDL3.txt",
    "imgui/LICENSE.txt": "Dear-ImGui.txt",
    "miniz/LICENSE": "miniz.txt",
    "fonts/OFL.txt": "Fira-Sans-OFL.txt",
}


def first_run(os_name, version):
    with open(os.path.join(HERE, f"FIRST-RUN-{os_name}.txt"), encoding="utf-8") as f:
        return f.read().replace("{version}", version)


def write_text(path, text, crlf=False):
    with open(path, "w", encoding="utf-8", newline="\r\n" if crlf else "\n") as f:
        f.write(text)


def copy_licenses(dest, extra=()):
    os.makedirs(dest, exist_ok=True)
    for src, name in LICENSES.items():
        p = os.path.join(TP, src)
        if os.path.exists(p):
            shutil.copy2(p, os.path.join(dest, name))
    # wgpu-native (MIT / Apache-2.0) and Lua (MIT) ship no license file in their drops.
    write_text(os.path.join(dest, "wgpu-native.txt"),
               "wgpu-native is dual-licensed under the MIT and Apache 2.0 licenses.\n"
               "https://github.com/gfx-rs/wgpu-native\n")
    write_text(os.path.join(dest, "Lua.txt"),
               "Lua is Copyright (C) 1994-2025 Lua.org, PUC-Rio, under the MIT license.\nhttps://www.lua.org/license.html\n")
    write_text(os.path.join(dest, "stb_vorbis.txt"),
               "stb_vorbis by Sean Barrett, public domain (or MIT).\nhttps://github.com/nothings/stb\n")
    # The HUD's atlas (res/hud_font.bin) is baked from Fira Mono: tools/make_font.py.
    shutil.copy2(os.path.join(ROOT, "res", "hud_font-OFL.txt"), os.path.join(dest, "Fira-Mono-OFL.txt"))
    for src, name in extra:
        shutil.copy2(src, os.path.join(dest, name))


def common_files(build, stage):
    shutil.copy2(os.path.join(build, "hud_font.bin"), stage)
    shutil.copytree(os.path.join(build, "fonts"), os.path.join(stage, "fonts"))


def windows(a):
    build = a.build if os.path.exists(os.path.join(a.build, "cleria-view.exe")) else os.path.join(a.build, "Release")
    stage = os.path.join(a.out, "stage", "CleriaCore")
    os.makedirs(stage)
    for f in ("cleria-view.exe", "cleria-gamedata.exe", "SDL3.dll", "wgpu_native.dll"):
        shutil.copy2(os.path.join(build, f), stage)
    common_files(build, stage)
    # The MSVC runtime the build links (/MD), deployed app-locally as Microsoft allows.
    redist = os.environ.get("VCToolsRedistDir")
    if not redist:
        vswhere = os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe")
        vs = subprocess.check_output([vswhere, "-latest", "-products", "*", "-property", "installationPath"], text=True).strip()
        redist = max(glob.glob(os.path.join(vs, "VC", "Redist", "MSVC", "14.*")))
    crt = glob.glob(os.path.join(redist, "x64", "Microsoft.VC14*.CRT", "*.dll"))
    if not crt:
        sys.exit(f"no MSVC runtime DLLs under {redist}")
    for f in crt:
        shutil.copy2(f, stage)
    write_text(os.path.join(stage, "FIRST-RUN.txt"), first_run("windows", a.version), crlf=True)
    copy_licenses(os.path.join(stage, "licenses"))
    out = os.path.join(a.out, f"CleriaCore-{a.version}-windows-x64.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for d, _, files in os.walk(stage):
            for f in files:
                p = os.path.join(d, f)
                z.write(p, os.path.relpath(p, os.path.dirname(stage)))
    return out


def ldd(path, dirs=()):
    """The resolved libraries a Linux ELF file loads: {soname: path}. `dirs` are searched first, as the
    bundled lib/ is once the rpaths are set (a library from the build tree may carry no rpath of its own)."""
    libs = {}
    env = dict(os.environ, LD_LIBRARY_PATH=os.pathsep.join([*dirs, os.environ.get("LD_LIBRARY_PATH", "")]))
    for line in subprocess.check_output(["ldd", path], text=True, env=env).splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] == "=>":
            if parts[2] == "not":
                sys.exit(f"{path}: {parts[0]} not found")
            libs[parts[0]] = os.path.realpath(parts[2])
    return libs


def linux(a):
    stage = os.path.join(a.out, "stage", "CleriaCore")
    lib = os.path.join(stage, "lib")
    os.makedirs(lib)
    exes = [os.path.join(stage, e) for e in ("cleria-view", "cleria-gamedata")]
    for e in exes:
        shutil.copy2(os.path.join(a.build, os.path.basename(e)), e)
    common_files(a.build, stage)
    # Bundle what the build provides (resolved inside the repo: the build dir, third_party), breadth
    # first; everything else comes from the player's system.
    mine = (os.path.realpath(a.build) + os.sep, os.path.realpath(TP) + os.sep)
    todo, bundled = list(exes), {}
    while todo:
        for soname, path in ldd(todo.pop(), sorted({os.path.dirname(p) for p in bundled.values()})).items():
            if path.startswith(mine) and soname not in bundled:
                bundled[soname] = path
                dst = os.path.join(lib, soname)
                shutil.copy2(path, dst)
                todo.append(dst)
    for e in exes:
        subprocess.check_call(["patchelf", "--set-rpath", "$ORIGIN/lib", e])
    for soname in bundled:
        subprocess.check_call(["patchelf", "--set-rpath", "$ORIGIN", os.path.join(lib, soname)])
    print("bundled:", ", ".join(sorted(bundled)))
    # Nothing may still resolve into the build tree.
    for f in exes + [os.path.join(lib, s) for s in bundled]:
        for soname, path in ldd(f).items():
            if path.startswith(mine):
                sys.exit(f"{f}: {soname} still loads {path}")
    write_text(os.path.join(stage, "FIRST-RUN.txt"), first_run("linux", a.version))
    ffmpeg_license = os.path.join(TP, "ffmpeg", "LICENSE-ffmpeg.txt")
    copy_licenses(os.path.join(stage, "licenses"),
                  [(ffmpeg_license, "FFmpeg-LGPL-2.1.txt")] if os.path.exists(ffmpeg_license) else [])
    out = os.path.join(a.out, f"CleriaCore-{a.version}-linux-x64.tar.gz")
    with tarfile.open(out, "w:gz") as t:
        t.add(stage, "CleriaCore")
    return out


def macos(a):
    stage = os.path.join(a.out, "stage", "CleriaCore")
    os.makedirs(stage)
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "macos_bundle.py"),
                           "--build", a.build, "--out", stage, "--version", a.version])
    for z in glob.glob(os.path.join(stage, "*.zip")):
        os.remove(z)
    write_text(os.path.join(stage, "FIRST-RUN.txt"), first_run("macos", a.version))
    copy_licenses(os.path.join(stage, "licenses"))   # beside the app: a file added inside breaks its signature
    out = os.path.join(a.out, f"CleriaCore-{a.version}-macos-{platform.machine()}.zip")
    subprocess.check_call(["ditto", "-c", "-k", "--keepParent", stage, out])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", default="build")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--version", required=True)
    a = ap.parse_args()
    shutil.rmtree(os.path.join(a.out, "stage"), ignore_errors=True)
    os.makedirs(a.out, exist_ok=True)
    pack = {"win32": windows, "linux": linux, "darwin": macos}[sys.platform]
    out = pack(a)
    print(f"{out}: {os.path.getsize(out) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
