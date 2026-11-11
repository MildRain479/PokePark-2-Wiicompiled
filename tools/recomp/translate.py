"""Translates Gekko/Broadway (PowerPC 750CL) instructions into C statements.

Each function becomes `void f_XXXXXXXX(ppc_t* c)`, operating on the guest
register file in `c` and guest memory through the helpers in
recomp/runtime/ppc.h. Branches inside the function become gotos, calls
become direct C calls, and indirect calls go through ppc_icall().

translate() returns None for any instruction it does not handle; the
caller emits a ppc_unimpl() call there so the output still compiles and
the gap shows up in the coverage report.
"""


def sext16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def mask(mb, me):
    m = 0
    i = mb
    while True:
        m |= 1 << (31 - i)
        if i == me:
            break
        i = (i + 1) & 31
    return m


def h32(v):
    return f"0x{v & 0xFFFFFFFF:08X}u"


def R(n):
    return f"c->r[{n}]"


def RA0(n):
    """(rA|0): register rA, or the constant 0 when rA is r0."""
    return "0u" if n == 0 else R(n)


def F(n):
    return f"c->f[{n}]"


def P1(n):
    return f"c->ps1[{n}]"


def imm_add(base, imm):
    if imm == 0:
        return base
    if base == "0u":
        return h32(imm)
    return f"{base} + {h32(imm)}"


def bc_cond(bo, bi):
    """Returns (prefix statement, condition) for a conditional branch."""
    pre = ""
    parts = []
    if not bo & 4:
        pre = "c->ctr--; "
        parts.append("c->ctr == 0" if bo & 2 else "c->ctr != 0")
    if not bo & 16:
        bit = f"CRBIT(c, {bi})"
        parts.append(bit if bo & 8 else f"!{bit}")
    return pre, " && ".join(parts)


class FuncCtx:
    """What the translator needs to know about the function being emitted."""

    def __init__(self, start, end, labels, entries, switch_targets):
        self.start = start
        self.end = end
        self.labels = labels  # set of addresses inside this function used as branch targets
        self.entries = entries  # set of all callable entry addresses
        self.switch_targets = switch_targets  # sorted jump table targets inside this function

    def inside(self, addr):
        return self.start <= addr < self.end


def call(ctx, target, nxt):
    if target in ctx.entries:
        return f"c->lr = {h32(nxt)}; f_{target:08X}(c);"
    return f"c->lr = {h32(nxt)}; ppc_unresolved(c, {h32(target)});"


def jump(ctx, target, addr):
    """A non-linking branch: a goto inside the function, a tail call otherwise."""
    if ctx.inside(target):
        return f"goto L_{target:08X};"
    if target in ctx.entries:
        return f"f_{target:08X}(c); return;"
    return f"ppc_unresolved(c, {h32(target)}); return;"


def branch_targets(addr, ins):
    """Direct branch target of a b/bc instruction (or None)."""
    op = ins >> 26
    if op == 18:
        li = sext(ins & 0x03FFFFFC, 26)
        return (li if ins & 2 else addr + li) & 0xFFFFFFFF, bool(ins & 1)
    if op == 16:
        bd = sext16(ins & 0xFFFC)
        return (bd if ins & 2 else addr + bd) & 0xFFFFFFFF, bool(ins & 1)
    return None


# ---------------------------------------------------------------------------
# Integer helpers


def rc0(reg):
    return f" RC0(c, {reg});"


def xo31_int(ins, xo, rc):
    d = (ins >> 21) & 31
    a = (ins >> 16) & 31
    b = (ins >> 11) & 31
    s = d
    rd, ra, rb, rs = R(d), R(a), R(b), R(s)
    x = xo & 0x1FF
    out = None
    # XO-form arithmetic (the OE bit is ignored; nothing sets XER[OV]).
    if x == 266:
        out = f"{rd} = {ra} + {rb};"
    elif x == 10:
        out = f"{{ uint32_t t = {ra} + {rb}; c->xer_ca = t < {ra}; {rd} = t; }}"
    elif x == 138:
        out = f"{{ uint64_t t = (uint64_t){ra} + {rb} + c->xer_ca; c->xer_ca = (uint32_t)(t >> 32); {rd} = (uint32_t)t; }}"
    elif x == 234:
        out = f"{{ uint64_t t = (uint64_t){ra} + 0xFFFFFFFFu + c->xer_ca; c->xer_ca = (uint32_t)(t >> 32); {rd} = (uint32_t)t; }}"
    elif x == 202:
        out = f"{{ uint64_t t = (uint64_t){ra} + c->xer_ca; c->xer_ca = (uint32_t)(t >> 32); {rd} = (uint32_t)t; }}"
    elif x == 40:
        out = f"{rd} = {rb} - {ra};"
    elif x == 8:
        out = f"{{ uint32_t t = {rb} - {ra}; c->xer_ca = {rb} >= {ra}; {rd} = t; }}"
    elif x == 136:
        out = f"{{ uint64_t t = (uint64_t)(uint32_t)~{ra} + {rb} + c->xer_ca; c->xer_ca = (uint32_t)(t >> 32); {rd} = (uint32_t)t; }}"
    elif x == 232:
        out = f"{{ uint64_t t = (uint64_t)(uint32_t)~{ra} + 0xFFFFFFFFu + c->xer_ca; c->xer_ca = (uint32_t)(t >> 32); {rd} = (uint32_t)t; }}"
    elif x == 200:
        out = f"{{ uint64_t t = (uint64_t)(uint32_t)~{ra} + c->xer_ca; c->xer_ca = (uint32_t)(t >> 32); {rd} = (uint32_t)t; }}"
    elif x == 104:
        out = f"{rd} = 0u - {ra};"
    elif x == 235:
        out = f"{rd} = {ra} * {rb};"  # unsigned: a signed overflow would let a compiler drop carry tests after it
    elif x == 75:
        out = f"{rd} = (uint32_t)(((int64_t)(int32_t){ra} * (int32_t){rb}) >> 32);"
    elif x == 11:
        out = f"{rd} = (uint32_t)(((uint64_t){ra} * {rb}) >> 32);"
    elif x == 491:
        out = f"{rd} = ppc_divw({ra}, {rb});"
    elif x == 459:
        out = f"{rd} = ppc_divwu({ra}, {rb});"
    if out is not None:
        return out + (rc0(rd) if rc else "")

    # X-form logical and shifts: rS is in the D field, the result goes to rA.
    if xo == 28:
        out = f"{ra} = {rs} & {rb};"
    elif xo == 60:
        out = f"{ra} = {rs} & ~{rb};"
    elif xo == 444:
        out = f"{ra} = {rs} | {rb};"
    elif xo == 412:
        out = f"{ra} = {rs} | ~{rb};"
    elif xo == 316:
        out = f"{ra} = {rs} ^ {rb};"
    elif xo == 124:
        out = f"{ra} = ~({rs} | {rb});"
    elif xo == 476:
        out = f"{ra} = ~({rs} & {rb});"
    elif xo == 284:
        out = f"{ra} = ~({rs} ^ {rb});"
    elif xo == 24:
        out = f"{ra} = ({rb} & 0x20) ? 0u : {rs} << ({rb} & 31);"
    elif xo == 536:
        out = f"{ra} = ({rb} & 0x20) ? 0u : {rs} >> ({rb} & 31);"
    elif xo == 792:
        out = f"{ra} = ppc_sraw(c, {rs}, {rb});"
    elif xo == 824:
        out = f"{ra} = ppc_srawi(c, {rs}, {b});"
    elif xo == 26:
        out = f"{ra} = ppc_cntlzw({rs});"
    elif xo == 922:
        out = f"{ra} = (uint32_t)(int32_t)(int16_t){rs};"
    elif xo == 954:
        out = f"{ra} = (uint32_t)(int32_t)(int8_t){rs};"
    if out is not None:
        return out + (rc0(ra) if rc else "")
    return None


def xo31_mem(ins, xo):
    d = (ins >> 21) & 31
    a = (ins >> 16) & 31
    b = (ins >> 11) & 31
    ea = f"{RA0(a)} + {R(b)}" if a else R(b)
    eau = f"{R(a)} + {R(b)}"
    loads = {23: ("LD32", ""), 87: ("LD8", ""), 279: ("LD16", ""), 343: ("LD16", "(uint32_t)(int32_t)(int16_t)"),
             534: ("LD32BR", ""), 790: ("LD16BR", ""), 20: ("LD32", "")}
    loads_u = {55: ("LD32", ""), 119: ("LD8", ""), 311: ("LD16", ""), 375: ("LD16", "(uint32_t)(int32_t)(int16_t)")}
    stores = {151: "ST32", 215: "ST8", 407: "ST16", 662: "ST32BR", 918: "ST16BR"}
    stores_u = {183: "ST32", 247: "ST8", 439: "ST16"}
    if xo in loads:
        fn, cast = loads[xo]
        return f"{R(d)} = {cast}{fn}({ea});"
    if xo in loads_u:
        fn, cast = loads_u[xo]
        return f"{{ uint32_t ea = {eau}; uint32_t v = {cast}{fn}(ea); {R(a)} = ea; {R(d)} = v; }}"
    if xo in stores:
        return f"{stores[xo]}({ea}, {R(d)});"
    if xo in stores_u:
        return f"{{ uint32_t ea = {eau}; {stores_u[xo]}(ea, {R(d)}); {R(a)} = ea; }}"
    if xo == 150:  # stwcx.: reservations always succeed
        return f"ST32({ea}, {R(d)}); cr_setfield(c, 0, 2 | c->xer_so);"
    # FP indexed loads and stores.
    if xo == 535:
        return f"{F(d)} = {P1(d)} = ld_f32({ea});"
    if xo == 567:
        return f"{{ uint32_t ea = {eau}; {F(d)} = {P1(d)} = ld_f32(ea); {R(a)} = ea; }}"
    if xo == 599:
        return f"{F(d)} = ld_f64({ea});"
    if xo == 631:
        return f"{{ uint32_t ea = {eau}; {F(d)} = ld_f64(ea); {R(a)} = ea; }}"
    if xo == 663:
        return f"st_f32({ea}, {F(d)});"
    if xo == 695:
        return f"{{ uint32_t ea = {eau}; st_f32(ea, {F(d)}); {R(a)} = ea; }}"
    if xo == 727:
        return f"st_f64({ea}, {F(d)});"
    if xo == 759:
        return f"{{ uint32_t ea = {eau}; st_f64(ea, {F(d)}); {R(a)} = ea; }}"
    if xo == 983:
        return f"ST32({ea}, (uint32_t)f64_bits({F(d)}));"
    if xo == 1014:  # dcbz
        return f"ppc_dcbz({ea});"
    if xo in (54, 86, 246, 278, 470, 982):  # dcbst, dcbf, dcbtst, dcbt, dcbi, icbi
        return ";"
    return None


def xo31_sys(ins, xo):
    d = (ins >> 21) & 31
    a = (ins >> 16) & 31
    spr = ((ins >> 16) & 0x1F) | (((ins >> 11) & 0x1F) << 5)
    if xo == 0:
        return f"cmp_s(c, {(ins >> 23) & 7}, (int32_t){R(a)}, (int32_t){R((ins >> 11) & 31)});"
    if xo == 32:
        return f"cmp_u(c, {(ins >> 23) & 7}, {R(a)}, {R((ins >> 11) & 31)});"
    if xo == 339:
        if spr == 8:
            return f"{R(d)} = c->lr;"
        if spr == 9:
            return f"{R(d)} = c->ctr;"
        return f"{R(d)} = ppc_mfspr(c, {spr});"
    if xo == 467:
        if spr == 8:
            return f"c->lr = {R(d)};"
        if spr == 9:
            return f"c->ctr = {R(d)};"
        return f"ppc_mtspr(c, {spr}, {R(d)});"
    if xo == 371:
        return f"{R(d)} = ppc_mftb(c, {spr});"
    if xo == 19:
        return f"{R(d)} = c->cr;"
    if xo == 144:
        fxm = (ins >> 12) & 0xFF
        m = 0
        for i in range(8):
            if fxm & (0x80 >> i):
                m |= 0xF0000000 >> (4 * i)
        return f"c->cr = (c->cr & {h32(~m)}) | ({R(d)} & {h32(m)});"
    if xo == 512:
        return f"cr_setfield(c, {(ins >> 23) & 7}, (c->xer_so << 3) | (c->xer_ca << 1)); c->xer_so = c->xer_ca = 0;"
    if xo == 83:
        return f"{R(d)} = c->msr;"
    if xo == 146:
        return f"c->msr = {R(d)};"
    if xo == 595:
        return f"{R(d)} = c->sr[{a & 15}];"
    if xo == 210:
        return f"c->sr[{a & 15}] = {R(d)};"
    if xo in (598, 854, 306, 566):  # sync, eieio, tlbie, tlbsync
        return ";"
    if xo == 4:
        return "ppc_trap(c, PC);"
    return None


# ---------------------------------------------------------------------------
# Floating point


def single(expr):
    return f"(double)(float)({expr})"


def fp63(ins):
    d = (ins >> 21) & 31
    a = (ins >> 16) & 31
    b = (ins >> 11) & 31
    cc = (ins >> 6) & 31
    xo = (ins >> 1) & 0x3FF
    x5 = (ins >> 1) & 31
    fd, fa, fb, fc = F(d), F(a), F(b), F(cc)
    ops5 = {
        18: f"{fd} = {fa} / {fb};",
        20: f"{fd} = {fa} - {fb};",
        21: f"{fd} = {fa} + {fb};",
        22: f"{fd} = sqrt({fb});",
        23: f"{fd} = ({fa} >= 0.0) ? {fc} : {fb};",
        25: f"{fd} = {fa} * {fc};",
        26: f"{fd} = 1.0 / sqrt({fb});",
        28: f"{fd} = {fa} * {fc} - {fb};",
        29: f"{fd} = {fa} * {fc} + {fb};",
        30: f"{fd} = -({fa} * {fc} - {fb});",
        31: f"{fd} = -({fa} * {fc} + {fb});",
    }
    if x5 in ops5 and x5 >= 18:
        return ops5[x5]
    crf = (ins >> 23) & 7
    if xo in (0, 32):
        return f"fcmp(c, {crf}, {fa}, {fb});"
    if xo == 12:
        return f"{fd} = {P1(d)} = {single(fb)};"
    if xo == 14:
        return f"{fd} = ppc_fctiw(c, {fb});"
    if xo == 15:
        return f"{fd} = ppc_fctiwz({fb});"
    if xo == 40:
        return f"{fd} = -{fb};"
    if xo == 72:
        return f"{fd} = {fb};"
    if xo == 136:
        return f"{fd} = -fabs({fb});"
    if xo == 264:
        return f"{fd} = fabs({fb});"
    if xo == 583:
        return f"{fd} = bits_f64(0xFFF8000000000000ull | c->fpscr);"
    if xo == 711:
        fm = (ins >> 17) & 0xFF
        m = 0
        for i in range(8):
            if fm & (0x80 >> i):
                m |= 0xF0000000 >> (4 * i)
        return f"c->fpscr = (c->fpscr & {h32(~m)}) | ((uint32_t)f64_bits({fb}) & {h32(m)});"
    if xo == 38:
        return f"c->fpscr |= 0x80000000u >> {d};"
    if xo == 70:
        return f"c->fpscr &= ~(0x80000000u >> {d});"
    if xo == 134:
        imm = (ins >> 12) & 0xF
        sh = 28 - 4 * crf
        return f"c->fpscr = (c->fpscr & ~(0xFu << {sh})) | ({imm}u << {sh});"
    if xo == 64:
        sf = (ins >> 18) & 7
        return f"cr_setfield(c, {crf}, (c->fpscr >> {28 - 4 * sf}) & 0xF);"
    return None


def fp59(ins):
    d = (ins >> 21) & 31
    a = (ins >> 16) & 31
    b = (ins >> 11) & 31
    cc = (ins >> 6) & 31
    x5 = (ins >> 1) & 31
    fa, fb, fc = F(a), F(b), F(cc)
    exprs = {
        18: f"{fa} / {fb}",
        20: f"{fa} - {fb}",
        21: f"{fa} + {fb}",
        22: f"sqrt({fb})",
        24: f"1.0 / {fb}",
        25: f"{fa} * {fc}",
        28: f"{fa} * {fc} - {fb}",
        29: f"{fa} * {fc} + {fb}",
        30: f"-({fa} * {fc} - {fb})",
        31: f"-({fa} * {fc} + {fb})",
    }
    if x5 not in exprs:
        return None
    # Single-precision results go to both halves of the paired register.
    return f"{F(d)} = {P1(d)} = {single(exprs[x5])};"


def ps4(ins):
    d = (ins >> 21) & 31
    a = (ins >> 16) & 31
    b = (ins >> 11) & 31
    cc = (ins >> 6) & 31
    xo = (ins >> 1) & 0x3FF
    x5 = (ins >> 1) & 31
    x6 = (ins >> 1) & 63
    a0, a1, b0, b1, c0, c1 = F(a), P1(a), F(b), P1(b), F(cc), P1(cc)

    def both(e0, e1):
        return f"{{ double t0 = {single(e0)}, t1 = {single(e1)}; {F(d)} = t0; {P1(d)} = t1; }}"

    if x6 in (6, 38, 7, 39):
        w = (ins >> 10) & 1
        i = (ins >> 7) & 7
        ea = f"{RA0(a)} + {R(b)}" if x6 in (6, 7) else f"{R(a)} + {R(b)}"
        if x6 in (6, 38):
            s = f"psq_load(c, {d}, ea, {w}, {i});"
        else:
            s = f"psq_store(c, {d}, ea, {w}, {i});"
        upd = f" {R(a)} = ea;" if x6 in (38, 39) else ""
        return f"{{ uint32_t ea = {ea}; {s}{upd} }}"

    a5 = {
        10: (f"{a0} + {b1}", c1),
        11: (c0, f"{a0} + {b1}"),
        12: (f"{a0} * {c0}", f"{a1} * {c0}"),
        13: (f"{a0} * {c1}", f"{a1} * {c1}"),
        14: (f"{a0} * {c0} + {b0}", f"{a1} * {c0} + {b1}"),
        15: (f"{a0} * {c1} + {b0}", f"{a1} * {c1} + {b1}"),
        18: (f"{a0} / {b0}", f"{a1} / {b1}"),
        20: (f"{a0} - {b0}", f"{a1} - {b1}"),
        21: (f"{a0} + {b0}", f"{a1} + {b1}"),
        23: (f"({a0} >= 0.0) ? {c0} : {b0}", f"({a1} >= 0.0) ? {c1} : {b1}"),
        24: (f"1.0 / {b0}", f"1.0 / {b1}"),
        25: (f"{a0} * {c0}", f"{a1} * {c1}"),
        26: (f"1.0 / sqrt({b0})", f"1.0 / sqrt({b1})"),
        28: (f"{a0} * {c0} - {b0}", f"{a1} * {c1} - {b1}"),
        29: (f"{a0} * {c0} + {b0}", f"{a1} * {c1} + {b1}"),
        30: (f"-({a0} * {c0} - {b0})", f"-({a1} * {c1} - {b1})"),
        31: (f"-({a0} * {c0} + {b0})", f"-({a1} * {c1} + {b1})"),
    }
    if x5 in a5 and x5 >= 10:
        e0, e1 = a5[x5]
        if x5 == 23:  # ps_sel copies values without rounding
            return f"{{ double t0 = {e0}, t1 = {e1}; {F(d)} = t0; {P1(d)} = t1; }}"
        return both(e0, e1)

    crf = (ins >> 23) & 7
    moves = {
        40: (f"-{b0}", f"-{b1}"),
        72: (b0, b1),
        136: (f"-fabs({b0})", f"-fabs({b1})"),
        264: (f"fabs({b0})", f"fabs({b1})"),
        528: (a0, b0),
        560: (a0, b1),
        592: (a1, b0),
        624: (a1, b1),
    }
    if xo in moves:
        e0, e1 = moves[xo]
        if xo >= 528:  # merges round to single
            return both(e0, e1)
        return f"{{ double t0 = {e0}, t1 = {e1}; {F(d)} = t0; {P1(d)} = t1; }}"
    if xo in (0, 32):
        return f"fcmp(c, {crf}, {a0}, {b0});"
    if xo in (64, 96):
        return f"fcmp(c, {crf}, {a1}, {b1});"
    if xo == 1014:  # dcbz_l
        ea = f"{RA0(a)} + {R(b)}" if a else R(b)
        return f"ppc_dcbz({ea});"
    return None


# ---------------------------------------------------------------------------


def translate(addr, ins, ctx):
    """Returns the C statement(s) for one instruction, or None."""
    op = ins >> 26
    d = (ins >> 21) & 31
    a = (ins >> 16) & 31
    b = (ins >> 11) & 31
    simm = sext16(ins)
    uimm = ins & 0xFFFF
    rc = ins & 1
    nxt = addr + 4

    if op == 14:
        return f"{R(d)} = {h32(simm)};" if a == 0 else f"{R(d)} = {imm_add(R(a), simm)};"
    if op == 15:
        return f"{R(d)} = {h32(uimm << 16)};" if a == 0 else f"{R(d)} = {imm_add(R(a), uimm << 16)};"
    if op in (12, 13):
        s = f"{{ uint32_t t = {R(a)} + {h32(simm)}; c->xer_ca = t < {R(a)}; {R(d)} = t; }}"
        return s + (rc0(R(d)) if op == 13 else "")
    if op == 8:
        return f"{{ uint32_t t = {h32(simm)} - {R(a)}; c->xer_ca = {h32(simm)} >= {R(a)}; {R(d)} = t; }}"
    if op == 7:
        return f"{R(d)} = {R(a)} * (uint32_t)({simm});"
    if op == 11:
        return f"cmp_s(c, {(ins >> 23) & 7}, (int32_t){R(a)}, {simm});"
    if op == 10:
        return f"cmp_u(c, {(ins >> 23) & 7}, {R(a)}, {h32(uimm)});"
    if op == 24:
        return f"{R(a)} = {R(d)} | {h32(uimm)};" if uimm or a != d else ";"
    if op == 25:
        return f"{R(a)} = {R(d)} | {h32(uimm << 16)};"
    if op == 26:
        return f"{R(a)} = {R(d)} ^ {h32(uimm)};"
    if op == 27:
        return f"{R(a)} = {R(d)} ^ {h32(uimm << 16)};"
    if op == 28:
        return f"{R(a)} = {R(d)} & {h32(uimm)};" + rc0(R(a))
    if op == 29:
        return f"{R(a)} = {R(d)} & {h32(uimm << 16)};" + rc0(R(a))
    if op in (20, 21, 23):
        sh = b
        mb = (ins >> 6) & 31
        me = (ins >> 1) & 31
        m = mask(mb, me)
        if op == 21:
            rot = f"ROTL({R(d)}, {sh})" if sh else R(d)
            s = f"{R(a)} = {rot} & {h32(m)};"
        elif op == 23:
            s = f"{R(a)} = ROTL({R(d)}, {R(b)} & 31) & {h32(m)};"
        else:
            rot = f"ROTL({R(d)}, {sh})" if sh else R(d)
            s = f"{R(a)} = ({rot} & {h32(m)}) | ({R(a)} & {h32(~m)});"
        return s + (rc0(R(a)) if rc else "")

    # D-form loads and stores.
    ea = imm_add(RA0(a), simm)
    eau = imm_add(R(a), simm)
    dloads = {32: ("LD32", ""), 34: ("LD8", ""), 40: ("LD16", ""), 42: ("LD16", "(uint32_t)(int32_t)(int16_t)")}
    dloads_u = {33: ("LD32", ""), 35: ("LD8", ""), 41: ("LD16", ""), 43: ("LD16", "(uint32_t)(int32_t)(int16_t)")}
    dstores = {36: "ST32", 38: "ST8", 44: "ST16"}
    dstores_u = {37: "ST32", 39: "ST8", 45: "ST16"}
    if op in dloads:
        fn, cast = dloads[op]
        return f"{R(d)} = {cast}{fn}({ea});"
    if op in dloads_u:
        fn, cast = dloads_u[op]
        return f"{{ uint32_t ea = {eau}; uint32_t v = {cast}{fn}(ea); {R(a)} = ea; {R(d)} = v; }}"
    if op in dstores:
        return f"{dstores[op]}({ea}, {R(d)});"
    if op in dstores_u:
        return f"{{ uint32_t ea = {eau}; {dstores_u[op]}(ea, {R(d)}); {R(a)} = ea; }}"
    if op == 46:
        regs = " ".join(f"{R(r)} = LD32(ea + {4 * (r - d)}u);" for r in range(d, 32))
        return f"{{ uint32_t ea = {ea}; {regs} }}"
    if op == 47:
        regs = " ".join(f"ST32(ea + {4 * (r - d)}u, {R(r)});" for r in range(d, 32))
        return f"{{ uint32_t ea = {ea}; {regs} }}"
    if op == 48:
        return f"{F(d)} = {P1(d)} = ld_f32({ea});"
    if op == 49:
        return f"{{ uint32_t ea = {eau}; {F(d)} = {P1(d)} = ld_f32(ea); {R(a)} = ea; }}"
    if op == 50:
        return f"{F(d)} = ld_f64({ea});"
    if op == 51:
        return f"{{ uint32_t ea = {eau}; {F(d)} = ld_f64(ea); {R(a)} = ea; }}"
    if op == 52:
        return f"st_f32({ea}, {F(d)});"
    if op == 53:
        return f"{{ uint32_t ea = {eau}; st_f32(ea, {F(d)}); {R(a)} = ea; }}"
    if op == 54:
        return f"st_f64({ea}, {F(d)});"
    if op == 55:
        return f"{{ uint32_t ea = {eau}; st_f64(ea, {F(d)}); {R(a)} = ea; }}"
    if op in (56, 57, 60, 61):
        w = (ins >> 15) & 1
        i = (ins >> 12) & 7
        off = sext(ins, 12)
        e = imm_add(RA0(a) if op in (56, 60) else R(a), off)
        fn = "psq_load" if op in (56, 57) else "psq_store"
        upd = f" {R(a)} = ea;" if op in (57, 61) else ""
        return f"{{ uint32_t ea = {e}; {fn}(c, {d}, ea, {w}, {i});{upd} }}"

    if op == 18 or op == 16:
        target, link = branch_targets(addr, ins)
        if op == 18:
            if link:
                return call(ctx, target, nxt)
            return jump(ctx, target, addr)
        bo, bi = d, a
        pre, cond = bc_cond(bo, bi)
        if link:
            body = f"c->lr = {h32(nxt)};" if target == nxt else call(ctx, target, nxt)
        else:
            body = jump(ctx, target, addr)
        if not cond:
            return pre + body
        return f"{pre}if ({cond}) {{ {body} }}"

    if op == 19:
        xo = (ins >> 1) & 0x3FF
        bo, bi = d, a
        link = ins & 1
        if xo == 16:
            pre, cond = bc_cond(bo, bi)
            if link:
                body = f"{{ uint32_t t = c->lr; c->lr = {h32(nxt)}; ppc_icall(c, t); }}"
            else:
                body = "return;"
            return pre + (f"if ({cond}) {body}" if cond else body)
        if xo == 528:
            pre, cond = bc_cond(bo | 4, bi)  # bcctr never decrements CTR
            if link:
                body = f"c->lr = {h32(nxt)}; ppc_icall(c, c->ctr);"
            elif ctx.switch_targets:
                cases = " ".join(f"case {h32(t)}: goto L_{t:08X};" for t in ctx.switch_targets)
                body = f"switch (c->ctr) {{ {cases} default: ppc_icall(c, c->ctr); return; }}"
            else:
                body = "ppc_icall(c, c->ctr); return;"
            return f"if ({cond}) {{ {body} }}" if cond else body
        cr_ops = {257: "{a} & {b}", 129: "{a} & !{b}", 289: "!({a} ^ {b})", 225: "!({a} & {b})",
                  33: "!({a} | {b})", 449: "{a} | {b}", 417: "{a} | !{b}", 193: "{a} ^ {b}"}
        if xo in cr_ops:
            e = cr_ops[xo].format(a=f"CRBIT(c, {a})", b=f"CRBIT(c, {b})")
            return f"cr_setbit(c, {d}, ({e}) & 1);"
        if xo == 0:
            return f"cr_setfield(c, {d >> 2}, (c->cr >> {28 - 4 * (a >> 2)}) & 0xF);"
        if xo == 150:
            return ";"
        if xo == 50:
            return "ppc_rfi(c); return;"
        return None

    if op == 31:
        xo = (ins >> 1) & 0x3FF
        for fn in (xo31_int, ):
            r = fn(ins, xo, rc)
            if r is not None:
                return r
        r = xo31_mem(ins, xo)
        if r is not None:
            return r
        return xo31_sys(ins, xo)
    if op == 63:
        return fp63(ins)
    if op == 59:
        return fp59(ins)
    if op == 4:
        return ps4(ins)
    if op == 3:
        return "ppc_trap(c, PC);"
    if op == 17:
        return "ppc_syscall(c);"
    return None
