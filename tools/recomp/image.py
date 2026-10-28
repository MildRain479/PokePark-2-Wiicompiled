"""Loads main.dol and the function list from dtk's symbols.txt."""

import re
import struct
from dataclasses import dataclass


@dataclass
class Section:
    addr: int
    data: bytes
    is_text: bool


class Dol:
    def __init__(self, path):
        raw = open(path, "rb").read()
        offs = struct.unpack_from(">18I", raw, 0x00)
        addrs = struct.unpack_from(">18I", raw, 0x48)
        sizes = struct.unpack_from(">18I", raw, 0x90)
        self.bss_addr, self.bss_size, self.entry = struct.unpack_from(">3I", raw, 0xD8)
        self.sections = []
        for i in range(18):
            if sizes[i]:
                self.sections.append(Section(addrs[i], raw[offs[i]:offs[i] + sizes[i]], i < 7))

    def section_at(self, addr):
        for s in self.sections:
            if s.addr <= addr < s.addr + len(s.data):
                return s
        return None

    def u32(self, addr):
        s = self.section_at(addr)
        if s is None:
            return None
        return struct.unpack_from(">I", s.data, addr - s.addr)[0]


@dataclass
class Symbol:
    name: str
    section: str
    addr: int
    size: int
    kind: str


_SYM_RE = re.compile(r"^(\S+) = (\.\w+):0x([0-9A-Fa-f]+); // type:(\w+)(?: size:0x([0-9A-Fa-f]+))?")


def load_symbols(path):
    syms = []
    for line in open(path, encoding="utf-8"):
        m = _SYM_RE.match(line)
        if m:
            syms.append(Symbol(m[1], m[2], int(m[3], 16), int(m[5] or "0", 16), m[4]))
    return syms
