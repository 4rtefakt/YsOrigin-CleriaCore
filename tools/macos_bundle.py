#!/usr/bin/env python3
"""Package a macOS build as CleriaCore.app (issue #46).

    python3 tools/macos_bundle.py [--build build] [--out dist] [--version X.Y.Z]

Contents/MacOS holds cleria-view (the bundle's executable) and cleria-gamedata; Contents/Resources what
cleria-view reads from its own folder (hud_font.bin, fonts/: SDL_GetBasePath() is Resources/ inside a
bundle); Contents/Frameworks every non-system dylib they load (SDL3, wgpu-native, and with CLERIA_FFMPEG /
CLERIA_CURL FFmpeg (tools/release/build_ffmpeg.sh in a release, else Homebrew's) and Homebrew's libcurl
with their own dependencies), each renamed @rpath/<name>. The
bundle is signed ad hoc, which Apple Silicon requires to run; it is not notarized, so a downloaded copy
needs "Open" from Finder's context menu the first time (or `xattr -dr com.apple.quarantine CleriaCore.app`).
Writes <out>/CleriaCore.app and <out>/CleriaCore-macos-<arch>.zip.
"""
import argparse
import os
import platform
import plistlib
import shutil
import subprocess
import sys

EXES = ["cleria-view", "cleria-gamedata"]
SYSTEM = ("/usr/lib/", "/System/")


def deps(path):
    """The load commands of a Mach-O file, minus its own id."""
    out = subprocess.check_output(["otool", "-L", path], text=True).splitlines()[1:]
    names = [line.strip().split(" (")[0] for line in out]
    own = subprocess.check_output(["otool", "-D", path], text=True).splitlines()[1:]
    return [n for n in names if n not in own]


def rpaths(path):
    lines = subprocess.check_output(["otool", "-l", path], text=True).splitlines()
    return [lines[i + 2].split()[1] for i, l in enumerate(lines) if l.strip() == "cmd LC_RPATH"]


def resolve(name, loader, search):
    """A load command's file on disk: absolute, or @rpath / @loader_path / @executable_path (the loader
    being where the file was built or installed, not its copy)."""
    if name.startswith("@rpath/"):
        for d in search:
            p = os.path.join(d, name[len("@rpath/"):])
            if os.path.exists(p):
                return os.path.realpath(p)
        return None
    if name.startswith(("@loader_path/", "@executable_path/")):
        p = os.path.join(os.path.dirname(loader), *name.split("/")[1:])
        return os.path.realpath(p) if os.path.exists(p) else None
    return os.path.realpath(name) if os.path.exists(name) else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", default="build")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--version", default="0.0.0")
    a = ap.parse_args()
    if sys.platform != "darwin":
        sys.exit("macos_bundle.py: macOS only")

    app = os.path.join(a.out, "CleriaCore.app")
    shutil.rmtree(app, ignore_errors=True)
    macos, res, fw = (os.path.join(app, "Contents", d) for d in ("MacOS", "Resources", "Frameworks"))
    for d in (macos, res, fw):
        os.makedirs(d)

    for e in EXES:
        shutil.copy2(os.path.join(a.build, e), macos)
    shutil.copy2(os.path.join(a.build, "hud_font.bin"), res)
    shutil.copytree(os.path.join(a.build, "fonts"), os.path.join(res, "fonts"))

    # Every non-system dylib reachable from the executables, breadth first. The build dir is where the
    # executables' @rpath / @loader_path entries point (SDL3 and wgpu-native are copied next to them).
    origin = {os.path.join(macos, e): os.path.join(a.build, e) for e in EXES}   # bundled file -> its source
    todo = list(origin)
    copied = {}  # realpath of a source dylib -> bundled file
    i = 0
    while i < len(todo):
        f = todo[i]
        i += 1
        src_f = origin[f]
        search = [os.path.join(os.path.dirname(src_f), *r.split("/")[1:]) if r.startswith("@") else r
                  for r in rpaths(f)] + [a.build]
        for name in deps(f):
            if name.startswith(SYSTEM):
                continue
            src = resolve(name, src_f, search)
            if not src:
                sys.exit(f"macos_bundle.py: {f}: cannot find {name}")
            if src not in copied:
                dst = os.path.join(fw, os.path.basename(name))
                shutil.copy2(src, dst)
                os.chmod(dst, 0o755)
                subprocess.check_call(["install_name_tool", "-id", "@rpath/" + os.path.basename(dst), dst])
                copied[src] = dst
                origin[dst] = src
                todo.append(dst)
            want = "@rpath/" + os.path.basename(copied[src])
            if name != want:
                subprocess.check_call(["install_name_tool", "-change", name, want, f])

    for e in EXES:
        exe = os.path.join(macos, e)
        for r in rpaths(exe):
            subprocess.check_call(["install_name_tool", "-delete_rpath", r, exe])
        subprocess.check_call(["install_name_tool", "-add_rpath", "@executable_path/../Frameworks", exe])
    for dst in copied.values():
        for r in rpaths(dst):
            subprocess.check_call(["install_name_tool", "-delete_rpath", r, dst])
        subprocess.check_call(["install_name_tool", "-add_rpath", "@loader_path", dst])

    with open(os.path.join(app, "Contents", "Info.plist"), "wb") as f:
        plistlib.dump({
            "CFBundleName": "CleriaCore",
            "CFBundleDisplayName": "CleriaCore",
            "CFBundleIdentifier": "io.github.4rtefakt.CleriaCore",
            "CFBundleExecutable": "cleria-view",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": a.version,
            "CFBundleVersion": a.version,
            "LSMinimumSystemVersion": "11.0",
            "LSApplicationCategoryType": "public.app-category.role-playing-games",
            "NSHighResolutionCapable": True,
            # .cleriamod files (a zip mod, build/shell_re/MODFILE.md): Finder opens them with CleriaCore, which
            # installs them (SDL hands the file over as a drop, view_modfile.inl modfile_dropped).
            "UTExportedTypeDeclarations": [{
                "UTTypeIdentifier": "io.github.4rtefakt.cleriamod",
                "UTTypeDescription": "CleriaCore mod",
                "UTTypeConformsTo": ["public.zip-archive", "public.data"],
                "UTTypeTagSpecification": {"public.filename-extension": ["cleriamod"]},
            }],
            "CFBundleDocumentTypes": [{
                "CFBundleTypeName": "CleriaCore mod",
                "CFBundleTypeRole": "Viewer",
                "LSHandlerRank": "Owner",
                "LSItemContentTypes": ["io.github.4rtefakt.cleriamod"],
            }],
        }, f)

    # Ad hoc signatures, inside out (install_name_tool invalidated the copied ones).
    for dst in copied.values():
        subprocess.check_call(["codesign", "--force", "--sign", "-", dst])
    for e in EXES:
        subprocess.check_call(["codesign", "--force", "--sign", "-", os.path.join(macos, e)])
    subprocess.check_call(["codesign", "--force", "--sign", "-", app])
    subprocess.check_call(["codesign", "--verify", "--deep", "--strict", app])

    arch = "arm64" if platform.machine() == "arm64" else "x86_64"
    z = os.path.join(a.out, f"CleriaCore-macos-{arch}.zip")
    if os.path.exists(z):
        os.remove(z)
    subprocess.check_call(["ditto", "-c", "-k", "--keepParent", app, z])
    print(f"{app}: {len(copied)} libraries bundled\n{z}")


if __name__ == "__main__":
    main()
