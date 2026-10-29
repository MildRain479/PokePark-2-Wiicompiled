"""Keeps a recompiled function's guest GPRs in C locals.

The translator writes every GPR access as c->r[N]. The C compiler can't
keep those in host registers: any store through guest memory might alias
the ppc_t. This pass rewrites them to locals (r0..r31) and syncs with
c->r only where other code can look at the registers:

- on entry, every register the function uses is loaded;
- before a call (direct, indirect or unresolved, a trap, rfi, sc) and
  before returning, every register the function writes is stored;
- after a call, every register it uses is reloaded, since the callee may
  have changed any of them (including r1 and r2, through interrupts and
  thread switches, which only happen inside calls).

No runtime helper that takes c reads or writes c->r (they work on CR,
XER, FPRs, GQRs and SPRs), so they need no sync.
"""

import re

REG = re.compile(r"c->r\[(\d+)\]")
# An assignment to a GPR (=, or a compound operator), not a comparison.
WRITE = re.compile(r"c->r\[(\d+)\]\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)")
# Statements that leave the function's code: calls into other guest code,
# and runtime calls that may run guest code or inspect the registers. An
# optional "return;" after one makes it a tail call.
CALL = re.compile(
    r"(f_[0-9A-F]{8}(?:_orig)?\(c\);"
    r"|ppc_(?:icall|unresolved|trap|unimpl)\(c, [^;]*\);"
    r"|ppc_(?:rfi|syscall)\(c\);)"
    r"( return;)?"
)
RETURN = re.compile(r"\breturn;")
MARK = "\x01"  # stands in for a tail call's return while bare returns are found


def cache_registers(lines):
    """lines: the function body (statements and labels). Returns the lines
    to put first (declarations and entry loads) and the rewritten body."""
    used, written = set(), set()
    for line in lines:
        used.update(int(n) for n in REG.findall(line))
        written.update(int(n) for n in WRITE.findall(line))
    if not used:
        return [], lines
    used = sorted(used)
    spill = " ".join(f"c->r[{n}] = r{n};" for n in sorted(written))
    reload = " ".join(f"r{n} = c->r[{n}];" for n in used)

    # Each replacement is one braced statement: the translator writes
    # conditional branches as "if (cond) return;" without braces.
    def around_call(m):
        call, ret = m.group(1), m.group(2)
        if ret:
            return f"{{ {spill} {call} {MARK} }}".replace("  ", " ")
        return f"{{ {spill} {call} {reload} }}".replace("  ", " ")

    out = []
    for line in lines:
        line = REG.sub(lambda m: f"r{m.group(1)}", line)
        line = CALL.sub(around_call, line)
        if spill:
            line = RETURN.sub(f"{{ {spill} return; }}", line)
        out.append(line.replace(MARK, "return;"))
    head = [f"    uint32_t {', '.join(f'r{n}' for n in used)};", f"    {reload}"]
    return head, out
