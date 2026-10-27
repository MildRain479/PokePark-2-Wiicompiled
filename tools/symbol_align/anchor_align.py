"""Name unnamed fn_XXXXXXXX functions by aligning them against reference
symbols.txt files from other dtk projects that share library code.

A run of unnamed functions is named only when it sits next to a named
"anchor" that also exists in the reference, the anchor's size matches, and
every function in the run has exactly the same size as its reference
counterpart. Strict mode additionally requires the anchor on the other side
of the run to line up. Names proposed by different references must agree.
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

LINE_RE = re.compile(r"^(\S+) = \.text:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)")
UNNAMED_RE = re.compile(r"^(fn|func|gap)_[0-9A-F]{8}")


def parse(path):
    funcs = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINE_RE.match(line)
        if m:
            funcs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
    funcs.sort(key=lambda f: f[1])
    return funcs


def unnamed(name):
    return bool(UNNAMED_RE.match(name)) or name.startswith("zz_") or name.startswith("lbl_")


def propose(target, ref, min_size, strict_only):
    ref_names = [f[0] for f in ref]
    ref_sizes = [f[2] for f in ref]
    ref_pos = defaultdict(list)
    for i, n in enumerate(ref_names):
        if not unnamed(n):
            ref_pos[n].append(i)

    out = {}
    n = len(target)
    i = 0
    while i < n:
        if not unnamed(target[i][0]):
            i += 1
            continue
        j = i
        while j < n and unnamed(target[j][0]):
            j += 1
        run = target[i:j]  # unnamed run, anchors at i-1 and j
        a = target[i - 1] if i > 0 else None
        b = target[j] if j < n else None

        # Strict: both anchors line up with exact gap
        done = False
        if a and len(ref_pos.get(a[0], [])) == 1:
            ra = ref_pos[a[0]][0]
            rb = ra + len(run) + 1
            if (b and rb < len(ref) and ref_names[rb] == b[0]
                    and ref_sizes[ra] == a[2] and ref_sizes[rb] == b[2]
                    and all(ref_sizes[ra + 1 + k] == run[k][2] for k in range(len(run)))):
                for k, f in enumerate(run):
                    rn = ref_names[ra + 1 + k]
                    if not unnamed(rn):
                        out[f[0]] = (rn, "strict")
                done = True

        if not done and not strict_only:
            # One-sided from the previous anchor
            if a and len(ref_pos.get(a[0], [])) == 1 and ref_sizes[ref_pos[a[0]][0]] == a[2]:
                ra = ref_pos[a[0]][0]
                for k, f in enumerate(run):
                    r = ra + 1 + k
                    if r >= len(ref) or ref_sizes[r] != f[2] or unnamed(ref_names[r]) or f[2] < min_size:
                        break
                    out[f[0]] = (ref_names[r], "forward")
            # One-sided from the next anchor
            if b and len(ref_pos.get(b[0], [])) == 1 and ref_sizes[ref_pos[b[0]][0]] == b[2]:
                rb = ref_pos[b[0]][0]
                for k, f in enumerate(reversed(run)):
                    r = rb - 1 - k
                    if r < 0 or ref_sizes[r] != f[2] or unnamed(ref_names[r]) or f[2] < min_size:
                        break
                    prev = out.get(f[0])
                    if prev and prev[0] != ref_names[r]:
                        out[f[0]] = (None, "conflict")
                    elif not prev:
                        out[f[0]] = (ref_names[r], "backward")
        i = j
    return out


def main():
    symbols_path = Path(sys.argv[1])
    refs = sys.argv[2:]
    min_size = 0x20
    lines = symbols_path.read_text(encoding="utf-8").splitlines()

    applied = {}
    for round_no in range(10):
        target = parse(symbols_path) if round_no == 0 else target
        existing = {f[0] for f in target}
        votes = defaultdict(dict)
        for ref_path in refs:
            ref = parse(ref_path)
            for old, (new, kind) in propose(target, ref, min_size, strict_only=False).items():
                votes[old][Path(ref_path).stem] = (new, kind)

        new_names = {}
        for old, v in votes.items():
            names = {nk[0] for nk in v.values()}
            if len(names) != 1 or None in names:
                continue
            name = names.pop()
            if name in existing or name in new_names.values():
                continue
            new_names[old] = (name, ",".join(f"{r}:{k}" for r, (_, k) in sorted(v.items())))
        if not new_names:
            break
        applied.update(new_names)
        target = [(new_names[f[0]][0] if f[0] in new_names else f[0], f[1], f[2]) for f in target]
        print(f"round {round_no + 1}: {len(new_names)} names", file=sys.stderr)

    rename = {old: new for old, (new, _) in applied.items()}
    out_lines = []
    for line in lines:
        m = re.match(r"^(\S+)( = .*)$", line)
        if m and m.group(1) in rename:
            line = rename[m.group(1)] + m.group(2)
        out_lines.append(line)
    symbols_path.write_text("\n".join(out_lines) + "\n", encoding="utf-8")

    for old, (new, why) in sorted(applied.items()):
        print(f"{old}\t{new}\t{why}")
    print(f"total renamed: {len(applied)}", file=sys.stderr)


if __name__ == "__main__":
    main()
