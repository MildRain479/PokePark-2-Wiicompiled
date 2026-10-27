"""Compare two DOL files and report differing ranges as virtual addresses.

Usage: python tools/dol_diff.py <expected.dol> <actual.dol> [max_ranges]
"""
import struct
import sys


def sections(data):
    offs = struct.unpack(">18I", data[0x00:0x48])
    addrs = struct.unpack(">18I", data[0x48:0x90])
    sizes = struct.unpack(">18I", data[0x90:0xD8])
    return [(o, a, s) for o, a, s in zip(offs, addrs, sizes) if s]


def to_addr(secs, off):
    for o, a, s in secs:
        if o <= off < o + s:
            return a + (off - o)
    return None


def main():
    a = open(sys.argv[1], "rb").read()
    b = open(sys.argv[2], "rb").read()
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    secs = sections(a)
    if sections(b) != secs:
        print("section headers differ:")
        for x, y in zip(sections(a), sections(b)):
            if x != y:
                print(f"  expected {x[1]:08X}+{x[2]:X} @{x[0]:X}  actual {y[1]:08X}+{y[2]:X} @{y[0]:X}")
    ranges = []
    i, n = 0, min(len(a), len(b))
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and a[j] != b[j]:
                j += 1
            ranges.append((i, j))
            i = j
        else:
            i += 1
    print(f"{len(ranges)} differing ranges")
    for s, e in ranges[:limit]:
        addr = to_addr(secs, s)
        where = f"{addr:08X}" if addr is not None else f"file+{s:X}"
        print(f"  {where}  len {e - s:#x}  expected {a[s:min(e, s + 8)].hex()}  actual {b[s:min(e, s + 8)].hex()}")


if __name__ == "__main__":
    main()
