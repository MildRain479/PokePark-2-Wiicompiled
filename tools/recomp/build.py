"""Builds the recompiled code, the test harness and the boot harness.

Usage:
    python tools/recomp/build.py                 # build both targets
    python tools/recomp/build.py --run test      # ...and run the tests
    python tools/recomp/build.py --run boot      # ...and boot main.dol
    python tools/recomp/build.py --release       # optimize across files (MSVC
                                                 # /GL /LTCG): a few % faster,
                                                 # each build ~70 s
    python tools/recomp/build.py --release --dist  # ...and put the game program
                                                 # in build/recomp/dist

Uses MSVC on Windows (found through vswhere) and cc elsewhere.
"""

import argparse
import glob
import shutil
import os
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "build", "recomp")
GEN = os.path.join(OUT, "gen")
OBJ = os.path.join(OUT, "obj")
DOL = os.path.join(ROOT, "orig", "R8AE01", "sys", "main.dol")

TARGETS = {
    "test": [
        os.path.join(ROOT, "recomp", "tests", "test_recomp.cpp"),
        # Decompiled game code, built natively as the reference.
        os.path.join(ROOT, "src", "flib", "CodepointConvert.cpp"),
        os.path.join(ROOT, "src", "flib", "FName.cpp"),
    ],
    "boot": sorted(glob.glob(os.path.join(ROOT, "recomp", "boot", "*.c")) +
                   glob.glob(os.path.join(ROOT, "recomp", "gpu", "*.c"))),
}
LIBS = {"boot": ["opengl32.lib", "user32.lib", "gdi32.lib", "ole32.lib", "windowscodecs.lib"]}
DEFINES = ["RECOMP_TRACE"]


def vcvars():
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    path = subprocess.check_output([vswhere, "-latest", "-products", "*", "-property", "installationPath"],
                                   text=True).strip()
    return os.path.join(path, "VC", "Auxiliary", "Build", "vcvars64.bat")


def stale(src, obj):
    return not os.path.exists(obj) or os.path.getmtime(obj) < os.path.getmtime(src)


def build_msvc(common, targets, opt):
    inc = (f'/I"{os.path.join(ROOT, "recomp", "runtime")}" /I"{GEN}" /I"{os.path.join(ROOT, "src")}" '
           f'/I"{os.path.join(ROOT, "recomp", "gpu")}"')
    defs = " ".join(f"/D{d}" for d in DEFINES)
    # The generated code relies on wrapping unsigned arithmetic only.
    flags = f"/nologo {opt} /MP /W0 /fp:precise /Zi {defs} {inc}"
    bat = os.path.join(OUT, "build.bat")
    objdir = OBJ
    with open(bat, "w") as f:
        f.write(f'@call "{vcvars()}" >nul 2>nul\n')

        def compile_(srcs, odir, lang):
            os.makedirs(odir, exist_ok=True)
            todo = [s for s in srcs if stale(s, os.path.join(odir, os.path.splitext(os.path.basename(s))[0] + ".obj"))]
            if todo:
                extra = "/TC" if lang == "c" else "/TP /EHsc /std:c++17"
                f.write(f'cl {flags} {extra} /Fd"{odir}\\\\" /Fo"{odir}\\\\" /c ' +
                        " ".join(f'"{s}"' for s in todo) + " || exit /b 1\n")

        compile_(common, objdir, "c")
        for name, srcs in targets.items():
            tdir = os.path.join(objdir, name)
            compile_([s for s in srcs if s.endswith(".c")], tdir, "c")
            compile_([s for s in srcs if not s.endswith(".c")], tdir, "cpp")
            exe = os.path.join(OUT, name + ".exe")
            libs = " ".join(LIBS.get(name, []))
            ltcg = "/LTCG" if "/GL" in opt else ""
            f.write(f'link /nologo /DEBUG /MANIFEST:EMBED {ltcg} /OUT:"{exe}" "{objdir}\\*.obj" "{tdir}\\*.obj" {libs} || exit /b 1\n')
    subprocess.check_call(["cmd", "/c", bat])


def build_cc(common, targets, opt):
    inc = ["-I", os.path.join(ROOT, "recomp", "runtime"), "-I", GEN, "-I", os.path.join(ROOT, "src")]
    defs = [f"-D{d}" for d in DEFINES]
    objdir = OBJ

    def compile_(srcs, odir):
        os.makedirs(odir, exist_ok=True)
        objs = []
        for s in srcs:
            o = os.path.join(odir, os.path.basename(s) + ".o")
            if stale(s, o):
                cmd = (["cc", "-std=c99"] if s.endswith(".c") else ["c++", "-std=c++17"])
                subprocess.check_call(cmd + [opt, "-g", "-w", "-c", s, "-o", o] + inc + defs)
            objs.append(o)
        return objs

    common_objs = compile_(common, objdir)
    for name, srcs in targets.items():
        objs = compile_(srcs, os.path.join(objdir, name))
        subprocess.check_call(["c++", "-o", os.path.join(OUT, name)] + common_objs + objs + ["-lm", "-lpthread"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", choices=sorted(TARGETS))
    ap.add_argument("--opt", default=None, help="optimization flag (default /O2 or -O2)")
    ap.add_argument("--release", action="store_true", help="whole-program optimization (MSVC /GL with /LTCG)")
    ap.add_argument("--dist", action="store_true",
                    help="copy the boot program, as PokePark.exe, and its files to build/recomp/dist")
    ap.add_argument("args", nargs="*", help="extra arguments for --run")
    args = ap.parse_args()
    global OBJ
    if args.release:
        args.opt = "/O2 /GL" if os.name == "nt" else "-O2 -flto"
    # Objects built with other flags go elsewhere, so switching rebuilds.
    if args.opt and args.opt not in ("/O2", "-O2"):
        OBJ = os.path.join(OUT, "obj_" + "".join(ch for ch in args.opt if ch.isalnum()))

    gen = sorted(glob.glob(os.path.join(GEN, "*.c")))
    if not gen:
        sys.exit("run tools/recomp/recomp.py first")
    common = gen + sorted(glob.glob(os.path.join(ROOT, "recomp", "runtime", "*.c")))
    common += sorted(glob.glob(os.path.join(ROOT, "recomp", "patches", "*.c")))
    # Headers aren't tracked; a changed header rebuilds everything.
    headers = (glob.glob(os.path.join(ROOT, "recomp", "runtime", "*.h")) + glob.glob(os.path.join(ROOT, "recomp", "gpu", "*.h")) +
               [os.path.join(GEN, "recomp_funcs.h")])
    newest = max(os.path.getmtime(h) for h in headers if os.path.exists(h))
    for o in glob.glob(os.path.join(OBJ, "**", "*.o*"), recursive=True):
        if os.path.getmtime(o) < newest:
            os.remove(o)
    if os.name == "nt":
        build_msvc(common, TARGETS, args.opt or "/O2")
    else:
        build_cc(common, TARGETS, args.opt or "-O2")
    # The button prompt icons, beside the program (prompts_win32.c).
    shutil.copytree(os.path.join(ROOT, "recomp", "assets", "prompts"), os.path.join(OUT, "prompts"), dirs_exist_ok=True)
    if args.dist:
        # Started without arguments it finds the game in an R8AE01 folder
        # beside it or above it (boot.c), so this runs in place.
        dist = os.path.join(OUT, "dist")
        os.makedirs(dist, exist_ok=True)
        shutil.copy2(os.path.join(OUT, "boot.exe"), os.path.join(dist, "PokePark.exe"))
        shutil.copytree(os.path.join(OUT, "prompts"), os.path.join(dist, "prompts"), dirs_exist_ok=True)
        # decomp-toolkit, which the game's set-up screen extracts disc images with (import_win32.c).
        dtk = os.path.join(ROOT, "build", "tools", "dtk.exe")
        if os.path.exists(dtk):
            os.makedirs(os.path.join(dist, "tools"), exist_ok=True)
            shutil.copy2(dtk, os.path.join(dist, "tools", "dtk.exe"))
            shutil.copy2(os.path.join(ROOT, "recomp", "assets", "dtk_license.txt"), os.path.join(dist, "tools", "dtk_license.txt"))
        print("dist:", dist)
    if args.run:
        exe = os.path.join(OUT, args.run + (".exe" if os.name == "nt" else ""))
        sys.exit(subprocess.call([exe, DOL] + args.args))


if __name__ == "__main__":
    main()
