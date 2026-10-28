"""Static recompiler proof of concept: main.dol -> C.

Usage:
    python tools/recomp/recomp.py [--out build/recomp/gen]

Reads orig/R8AE01/sys/main.dol and config/R8AE01/symbols.txt, and writes
one C function per guest function, a header of prototypes, the address
table used for indirect calls, and coverage.txt. The generated code
contains no game data; the runtime loads main.dol from the user's own
copy at startup.
"""

import argparse
import bisect
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image import Dol, load_symbols  # noqa: E402
from translate import FuncCtx, branch_targets, h32, translate  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
FUNCS_PER_FILE = 200


def opcode_key(ins):
    op = ins >> 26
    if op in (4, 19, 31, 63):
        return f"op{op}/xo{(ins >> 1) & 0x3FF}"
    if op == 59:
        return f"op59/xo{(ins >> 1) & 31}"
    return f"op{op}"


class Func:
    def __init__(self, name, start, end, alt_of=None):
        self.name = name
        self.start = start
        self.end = end
        self.alt_of = alt_of


def collect(dol, syms):
    funcs = {}
    for s in syms:
        if s.kind == "function" and s.section in (".init", ".text") and s.size:
            funcs[s.addr] = Func(s.name, s.addr, s.addr + s.size)
    starts = sorted(funcs)

    def owner(addr):
        i = bisect.bisect_right(starts, addr) - 1
        if i >= 0:
            f = funcs[starts[i]]
            if f.start <= addr < f.end:
                return f
        return None

    # Calls or tail calls into the middle of a function (e.g. _savegpr_25
    # inside __save_gpr) become extra entry points running to the end of
    # the containing function.
    labels = {s.addr: s.name for s in syms if s.kind == "label"}
    alts = {}
    for f in list(funcs.values()):
        for addr in range(f.start, f.end, 4):
            ins = dol.u32(addr)
            bt = branch_targets(addr, ins)
            if not bt:
                continue
            target, link = bt
            if target in funcs or (not link and f.start <= target < f.end):
                continue
            o = owner(target)
            if o is not None and target not in alts:
                alts[target] = Func(labels.get(target, f"{o.name}+0x{target - o.start:X}"), target, o.end, o)
    funcs.update(alts)

    # Jump table targets, grouped by the function they land in. Tables are
    # not all named (MWCC's own are "@NNN"), so any data word pointing
    # inside a function that ends a block with bctr counts as a possible
    # case; an extra case only costs a switch entry.
    has_bctr = set()
    for f in funcs.values():
        if f.alt_of is None and any(dol.u32(a) == 0x4E800420 for a in range(f.start, f.end, 4)):
            has_bctr.add(f.start)
    switch = collections.defaultdict(set)
    for sec in dol.sections:
        if sec.is_text:
            continue
        for off in range(0, len(sec.data) - 3, 4):
            t = int.from_bytes(sec.data[off:off + 4], "big")
            o = owner(t)
            if o is not None and o.start in has_bctr and t != o.start and t % 4 == 0:
                switch[o.start].add(t)
    return funcs, switch


def load_hooks(path):
    """Addresses listed in recomp/patches/hooks.txt ("0xADDR name" per line)."""
    hooks = set()
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.split("#")[0].strip()
            if line:
                hooks.add(int(line.split()[0], 16))
    return hooks


def emit_function(dol, f, entries, switch, stats, hooked=False):
    ctx_switch = sorted(t for t in switch.get(f.alt_of.start if f.alt_of else f.start, ()) if f.start <= t < f.end)
    labels = set(ctx_switch)
    for addr in range(f.start, f.end, 4):
        bt = branch_targets(addr, dol.u32(addr))
        if bt and not bt[1] and f.start <= bt[0] < f.end:
            labels.add(bt[0])
    ctx = FuncCtx(f.start, f.end, labels, entries, ctx_switch)

    # A hooked function keeps its recompiled body under f_XXXXXXXX_orig;
    # the patch source defines f_XXXXXXXX, so every direct and indirect
    # call in the game reaches the patch.
    suffix = "_orig" if hooked else ""
    lines = [f"/* {f.name} */", f"void f_{f.start:08X}{suffix}(ppc_t* c) {{"]
    missing = 0
    for addr in range(f.start, f.end, 4):
        ins = dol.u32(addr)
        if addr in labels:
            lines.append(f"L_{addr:08X}:;")
        stmt = translate(addr, ins, ctx)
        stats["insns"] += 1
        if stmt is None:
            missing += 1
            stats["missing"][opcode_key(ins)] += 1
            stmt = f"ppc_unimpl(c, {h32(addr)}, {h32(ins)});"
        stmt = stmt.replace("ppc_trap(c, PC)", f"ppc_trap(c, {h32(addr)})")
        lines.append(f"    {stmt}")
    # Code that runs off the end continues in whatever function follows.
    if f.end in entries:
        lines.append(f"    f_{f.end:08X}(c);")
    lines.append("}")
    stats["funcs"] += 1
    if missing == 0:
        stats["complete"] += 1
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dol", default=os.path.join(ROOT, "orig/R8AE01/sys/main.dol"))
    ap.add_argument("--symbols", default=os.path.join(ROOT, "config/R8AE01/symbols.txt"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build/recomp/gen"))
    ap.add_argument("--hooks", default=os.path.join(ROOT, "recomp/patches/hooks.txt"))
    args = ap.parse_args()

    dol = Dol(args.dol)
    syms = load_symbols(args.symbols)
    funcs, switch = collect(dol, syms)
    entries = set(funcs)
    hooks = load_hooks(args.hooks)
    for a in hooks:
        if a not in funcs:
            sys.exit(f"hook address {a:08X} is not a function entry")
    order = sorted(funcs)
    os.makedirs(args.out, exist_ok=True)

    stats = {"insns": 0, "funcs": 0, "complete": 0, "missing": collections.Counter()}

    with open(os.path.join(args.out, "recomp_funcs.h"), "w", newline="\n") as h:
        h.write("/* Generated by tools/recomp/recomp.py. Do not edit. */\n#pragma once\n#include \"ppc.h\"\n\n")
        for a in order:
            h.write(f"void f_{a:08X}(ppc_t* c);\n")
            if a in hooks:
                h.write(f"void f_{a:08X}_orig(ppc_t* c);\n")

    nfiles = 0
    for i in range(0, len(order), FUNCS_PER_FILE):
        chunk = order[i:i + FUNCS_PER_FILE]
        with open(os.path.join(args.out, f"recomp_{nfiles:03d}.c"), "w", newline="\n") as out:
            out.write("/* Generated by tools/recomp/recomp.py. Do not edit. */\n#include \"recomp_funcs.h\"\n\n")
            for a in chunk:
                out.write(emit_function(dol, funcs[a], entries, switch, stats, a in hooks))
                out.write("\n\n")
        nfiles += 1

    with open(os.path.join(args.out, "recomp_table.c"), "w", newline="\n") as t:
        t.write("/* Generated by tools/recomp/recomp.py. Do not edit. */\n#include \"recomp_funcs.h\"\n\n")
        t.write("const ppc_func_entry ppc_func_table[] = {\n")
        for a in order:
            name = funcs[a].name.replace("\\", "\\\\").replace('"', '\\"')
            t.write(f'    {{ {h32(a)}, f_{a:08X}, "{name}" }},\n')
        t.write("};\n")
        t.write(f"const unsigned ppc_func_count = {len(order)};\n")

    with open(os.path.join(args.out, "coverage.txt"), "w", newline="\n") as cov:
        miss = sum(stats["missing"].values())
        cov.write(f"functions: {stats['funcs']} ({len(order) - len([f for f in funcs.values() if f.alt_of])} symbols + "
                  f"{len([f for f in funcs.values() if f.alt_of])} extra entry points)\n")
        cov.write(f"fully translated functions: {stats['complete']} ({100 * stats['complete'] / stats['funcs']:.2f}%)\n")
        cov.write(f"instructions: {stats['insns']}, untranslated: {miss} ({100 * miss / stats['insns']:.3f}%)\n")
        cov.write(f"jump tables used: {sum(len(v) for v in switch.values())} targets in {len(switch)} functions\n")
        cov.write("untranslated by opcode:\n")
        for k, v in stats["missing"].most_common():
            cov.write(f"  {k}: {v}\n")
    print(open(os.path.join(args.out, "coverage.txt")).read())
    print(f"wrote {nfiles} source files to {args.out}")


if __name__ == "__main__":
    main()
