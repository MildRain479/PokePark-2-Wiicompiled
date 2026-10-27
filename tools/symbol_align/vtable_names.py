"""Name unnamed functions from the vtables they appear in.

Follows the project's existing convention:
  Class::virt0xOFF          slot OFF of the primary vtable
  Class::virt0x{4k}_0xOFF   slot OFF of the k-th secondary vtable
Sub-vtables are delimited by their two-word header (RTTI is off, so both
words are zero). Offsets are relative to the start of each sub-vtable.

When a function appears in several vtables, the owning class is chosen with
the rule that best reproduces existing names (see --evaluate).

Usage:
  python -I vtable_names.py <symbols.txt> <asm_dir> [--evaluate] [--apply]
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

FN_RE = re.compile(r"^(\S+) = \.text:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)")
OBJ_START = re.compile(r"^\.obj (\S+), \w+")
OBJ_END = re.compile(r"^\.endobj ")
WORD = re.compile(r"^\s+\.4byte (\S+)")
UNNAMED = re.compile(r"^fn_[0-9A-F]{8}$")


def read_vtables(asm_dir):
    vtables = {}
    for path in Path(asm_dir).glob("*.s"):
        cur = None
        words = []
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            m = OBJ_START.match(line)
            if m:
                cur, words = m.group(1), []
                continue
            if cur is None:
                continue
            if OBJ_END.match(line):
                if cur.endswith("::__vt"):
                    vtables[cur[: -len("::__vt")]] = words
                cur = None
                continue
            w = WORD.match(line)
            if w:
                words.append(w.group(1))
            elif line.strip() and not line.startswith("#"):
                words.append(None)  # non-word data; breaks slot counting
    return vtables


def slots(words):
    """Yield (sub_index, offset, symbol) for each function slot."""
    sub = -1
    start = 0
    i = 0
    while i < len(words):
        if words[i] == "0x00000000" and i + 1 < len(words) and words[i + 1] == "0x00000000" \
                and (i == 0 or words[i - 1] != "0x00000000"):
            sub += 1
            start = i
            i += 2
            continue
        w = words[i]
        if w and not w.startswith("0x") and sub >= 0:
            yield sub, (i - start) * 4, w
        i += 1


def slot_name(cls, sub, off):
    return f"{cls}::virt0x{off:X}" if sub == 0 else f"{cls}::virt0x{4 * sub:X}_0x{off:X}"


def main():
    symbols_path = Path(sys.argv[1])
    asm_dir = sys.argv[2]
    evaluate = "--evaluate" in sys.argv
    apply = "--apply" in sys.argv

    lines = symbols_path.read_text(encoding="utf-8").splitlines()
    names = set()
    for line in lines:
        m = FN_RE.match(line)
        if m:
            names.add(m.group(1))
    all_names = {l.split(" = ")[0] for l in lines if " = " in l}

    vtables = read_vtables(asm_dir)
    vt_size = {c: len(w) for c, w in vtables.items()}
    occurrences = defaultdict(list)  # func -> [(cls, sub, off)]
    for cls, words in vtables.items():
        for sub, off, sym in slots(words):
            if sym in names:
                occurrences[sym].append((cls, sub, off))

    rules = {
        "smallest_vtable": lambda occ: min(occ, key=lambda o: (vt_size[o[0]], o[0])),
        "first_listed": lambda occ: occ[0],
    }

    if evaluate:
        for rule_name, rule in rules.items():
            total = exact = owner = 0
            for sym, occ in occurrences.items():
                if UNNAMED.match(sym) or "::virt0x" not in sym:
                    continue
                total += 1
                cls, sub, off = rule(occ)
                if slot_name(cls, sub, off) == sym:
                    exact += 1
                if sym.startswith(cls + "::"):
                    owner += 1
            print(f"{rule_name}: {exact}/{total} exact, {owner}/{total} right class")
        single = [(s, o) for s, o in occurrences.items() if len(o) == 1 and "::virt0x" in s]
        ok = sum(1 for s, o in single if slot_name(*o[0]) == s)
        print(f"single-vtable functions: {ok}/{len(single)} exact")

    proposals = {}
    used = set(all_names)
    for sym, occ in sorted(occurrences.items()):
        if not UNNAMED.match(sym):
            continue
        cls, sub, off = rules["smallest_vtable"](occ)
        new = slot_name(cls, sub, off)
        if new in used:
            continue
        used.add(new)
        proposals[sym] = (new, len(occ))

    print(f"proposed: {len(proposals)} "
          f"(single vtable: {sum(1 for v in proposals.values() if v[1] == 1)})", file=sys.stderr)
    for old, (new, n) in sorted(proposals.items()):
        print(f"{old}\t{new}\t{n}")

    if apply:
        out = []
        for line in lines:
            k = line.split(" = ")[0] if " = " in line else None
            if k in proposals:
                line = proposals[k][0] + line[len(k):]
            out.append(line)
        symbols_path.write_text("\n".join(out) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
