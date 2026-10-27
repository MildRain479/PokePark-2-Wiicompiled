"""Summarise an objdiff-cli one-shot JSON diff.

Usage:
  objdiff-cli diff -p . -u <unit> -o diff.json --format json
  python tools/objdiff_summary.py diff.json [--insns]

Prints per-section and per-symbol match percentages for the target (left)
side, and with --insns, the mismatching instructions of each function.
"""
import json
import sys


def main():
    path = sys.argv[1]
    show_insns = "--insns" in sys.argv
    with open(path, encoding="utf-8") as f:
        diff = json.load(f)
    left, right = diff.get("left", {}), diff.get("right", {})
    right_syms = right.get("symbols", [])

    for sec in left.get("sections", []):
        if "match_percent" in sec:
            print(f"section {sec['name']:<8} {sec['match_percent']:6.2f}%  size {sec['size']}")

    for sym in left.get("symbols", []):
        if sym.get("kind") not in ("SYMBOL_FUNCTION", "SYMBOL_OBJECT"):
            continue
        pct = sym.get("match_percent")
        other = sym.get("target_symbol")
        other_name = right_syms[other]["name"] if other is not None and other < len(right_syms) else "-"
        pct_s = f"{pct:6.2f}%" if pct is not None else "   n/a "
        print(f"{pct_s}  {sym['name']}  <->  {other_name}")
        if show_insns and pct is not None and pct < 100 and sym.get("instructions"):
            r_insns = right_syms[other].get("instructions", []) if other is not None else []
            for i, ins in enumerate(sym["instructions"]):
                kind = ins.get("diff_kind", "")
                if kind and kind != "DIFF_NONE":
                    l = ins.get("instruction", {}).get("formatted", "")
                    r = ""
                    if i < len(r_insns):
                        r = r_insns[i].get("instruction", {}).get("formatted", "")
                    print(f"      {kind:<16} {l:<40} | {r}")


if __name__ == "__main__":
    main()
