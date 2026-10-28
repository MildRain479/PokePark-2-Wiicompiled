"""Builds the recompiled code and the test harness.

Usage:
    python tools/recomp/build.py            # build build/recomp/test_recomp(.exe)
    python tools/recomp/build.py --run      # ...and run it against orig/R8AE01/sys/main.dol

Uses MSVC on Windows (found through vswhere) and cc elsewhere.
"""

import argparse
import glob
import os
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "build", "recomp")
GEN = os.path.join(OUT, "gen")


def vcvars():
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    path = subprocess.check_output([vswhere, "-latest", "-products", "*", "-property", "installationPath"],
                                   text=True).strip()
    return os.path.join(path, "VC", "Auxiliary", "Build", "vcvars64.bat")


def build_msvc(sources_c, sources_cpp, exe, opt):
    objdir = os.path.join(OUT, "obj")
    os.makedirs(objdir, exist_ok=True)
    inc = f'/I"{os.path.join(ROOT, "recomp", "runtime")}" /I"{GEN}" /I"{os.path.join(ROOT, "src")}"'
    # The generated code relies on wrapping unsigned arithmetic only.
    common = f"/nologo {opt} /MP /W0 /fp:precise /Fo\"{objdir}\\\\\" {inc}"
    bat = os.path.join(OUT, "build.bat")
    with open(bat, "w") as f:
        f.write(f'@call "{vcvars()}" >nul\n')
        f.write(f"cl {common} /c /TC " + " ".join(f'"{s}"' for s in sources_c) + " || exit /b 1\n")
        f.write(f"cl {common} /c /TP /EHsc /std:c++17 " + " ".join(f'"{s}"' for s in sources_cpp) + " || exit /b 1\n")
        f.write(f'link /nologo /OUT:"{exe}" "{objdir}\\*.obj" || exit /b 1\n')
    subprocess.check_call(["cmd", "/c", bat])


def build_cc(sources_c, sources_cpp, exe, opt):
    inc = ["-I", os.path.join(ROOT, "recomp", "runtime"), "-I", GEN, "-I", os.path.join(ROOT, "src")]
    objdir = os.path.join(OUT, "obj")
    os.makedirs(objdir, exist_ok=True)
    objs = []
    for s in sources_c + sources_cpp:
        o = os.path.join(objdir, os.path.basename(s) + ".o")
        cmd = (["cc", "-std=c99"] if s.endswith(".c") else ["c++", "-std=c++17"]) + [opt, "-w", "-c", s, "-o", o] + inc
        subprocess.check_call(cmd)
        objs.append(o)
    subprocess.check_call(["c++", "-o", exe] + objs + ["-lm"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--opt", default=None, help="optimization flag (default /O2 or -O2)")
    args = ap.parse_args()

    gen = sorted(glob.glob(os.path.join(GEN, "*.c")))
    if not gen:
        sys.exit("run tools/recomp/recomp.py first")
    sources_c = gen + [os.path.join(ROOT, "recomp", "runtime", "runtime.c")]
    sources_c += sorted(glob.glob(os.path.join(ROOT, "recomp", "patches", "*.c")))
    sources_cpp = [
        os.path.join(ROOT, "recomp", "tests", "test_recomp.cpp"),
        # Decompiled game code, built natively as the reference.
        os.path.join(ROOT, "src", "flib", "CodepointConvert.cpp"),
        os.path.join(ROOT, "src", "flib", "FName.cpp"),
    ]
    exe = os.path.join(OUT, "test_recomp" + (".exe" if os.name == "nt" else ""))
    if os.name == "nt":
        build_msvc(sources_c, sources_cpp, exe, args.opt or "/O2")
    else:
        build_cc(sources_c, sources_cpp, exe, args.opt or "-O2")
    print("built", exe)
    if args.run:
        sys.exit(subprocess.call([exe, os.path.join(ROOT, "orig", "R8AE01", "sys", "main.dol")]))


if __name__ == "__main__":
    main()
