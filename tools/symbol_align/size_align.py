"""Second-pass function namer: size-sequence alignment against reference dtk
symbols.txt files.

For every window of W consecutive target functions, look for the same
sequence of exact sizes in a reference. A window is accepted only if:
  * the size sequence is unique in both the target and the reference,
  * it is distinctive (enough total bytes and distinct sizes),
  * every already-named target function in the window agrees with the
    reference name after normalisation (and at least one such check exists,
    unless the window is very long).
Unnamed functions in accepted windows take the reference name. Different
references must agree on a name or it is dropped.
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

LINE_RE = re.compile(r"^(\S+) = \.text:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)")
UNNAMED_RE = re.compile(r"^(fn|func|gap|zz|lbl)_[0-9A-Fa-f]{8}")


def parse(path):
    funcs = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINE_RE.match(line)
        if m:
            funcs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
    funcs.sort(key=lambda f: f[1])
    return funcs


def unnamed(name):
    return bool(UNNAMED_RE.match(name))


def _read_qual(s, i):
    """Parse a CodeWarrior qualified name at s[i:], return (parts, new_i)."""
    if i < len(s) and s[i] == "Q":
        count = int(s[i + 1])
        i += 2
        parts = []
        for _ in range(count):
            m = re.match(r"\d+", s[i:])
            if not m:
                return None, i
            n = int(m.group())
            i += len(m.group())
            parts.append(s[i:i + n])
            i += n
        return parts, i
    m = re.match(r"\d+", s[i:])
    if not m:
        return None, i
    n = int(m.group())
    i += len(m.group())
    return [s[i:i + n]], i + n


def normalise(name):
    """Reduce a symbol to 'ns::Class::func' ignoring parameters."""
    if "::" in name and "__" not in name.split("::")[-1]:
        return name.lower()
    # CodeWarrior mangled: func__<qual>F<params> or func__F<params>
    m = re.match(r"^(.+?)__([Q0-9].*)$", name)
    if m and not name.startswith("__"):
        parts, _ = _read_qual(m.group(2), 0)
        if parts:
            return "::".join(parts + [m.group(1)]).lower()
    if name.startswith("__") and "__" in name[2:]:
        base, rest = name[2:].split("__", 1)
        parts, _ = _read_qual(rest, 0)
        if parts:
            return "::".join(parts + ["__" + base]).lower()
    m = re.match(r"^(.+?)__F", name)
    if m:
        return m.group(1).lower()
    # Ghidra-style flattened: nw4r__snd__detail__Class__Func
    if "__" in name.strip("_"):
        return "::".join(p for p in name.split("__") if p).lower()
    return name.lower()


def windows(funcs, w):
    sizes = [f[2] for f in funcs]
    idx = defaultdict(list)
    for i in range(len(funcs) - w + 1):
        idx[tuple(sizes[i:i + w])].append(i)
    return idx


def propose(target, ref, w):
    t_idx = windows(target, w)
    r_idx = windows(ref, w)
    out = {}
    for key, t_list in t_idx.items():
        if len(t_list) != 1:
            continue
        r_list = r_idx.get(key)
        if not r_list or len(r_list) != 1:
            continue
        if sum(key) < 0x300 or len(set(key)) < max(4, w // 2):
            continue
        ti, ri = t_list[0], r_list[0]
        checks = agree = 0
        for k in range(w):
            tn, rn = target[ti + k][0], ref[ri + k][0]
            if not unnamed(tn) and not unnamed(rn):
                checks += 1
                if normalise(tn) == normalise(rn) or normalise(tn).split("::")[-1] == normalise(rn).split("::")[-1]:
                    agree += 1
        if checks and agree < checks:
            continue
        if checks == 0 and w < 12:
            continue
        for k in range(w):
            tn, rn = target[ti + k][0], ref[ri + k][0]
            if unnamed(tn) and not unnamed(rn):
                prev = out.get(tn)
                if prev and prev != rn:
                    out[tn] = None
                else:
                    out[tn] = rn
    return out


def main():
    symbols_path = Path(sys.argv[1])
    refs = [(Path(p).stem, parse(p)) for p in sys.argv[2:]]
    lines = symbols_path.read_text(encoding="utf-8").splitlines()
    target = parse(symbols_path)

    applied = {}
    for round_no in range(10):
        existing = {f[0] for f in target}
        existing_norm = {normalise(f[0]) for f in target if not unnamed(f[0])}
        votes = defaultdict(dict)
        for ref_name, ref in refs:
            merged = {}
            for w in (4, 5, 6, 8, 12, 16):
                for old, new in propose(target, ref, w).items():
                    if new is None or (old in merged and merged[old] != new):
                        merged[old] = None
                    else:
                        merged[old] = new
            for old, new in merged.items():
                if new:
                    votes[old][ref_name] = new
        new_names = {}
        used = set()
        for old, v in votes.items():
            names = set(v.values())
            if len(names) != 1:
                continue
            name = names.pop()
            if name in existing or name in used:
                continue
            if normalise(name) in existing_norm and not name.startswith("__"):
                continue
            new_names[old] = (name, ",".join(sorted(v)))
            used.add(name)
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

