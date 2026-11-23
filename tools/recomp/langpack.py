"""Builds language packs: the USA game's text and voices in another
language, taken from the player's own disc of that language.

    python tools/recomp/langpack.py USA_FILES OTHER_FILES LANGUAGES

USA_FILES and OTHER_FILES are the "files" folders of the two extracted
discs (dtk's layout: <disc>/files); LANGUAGES is the folder the packs go
in, languages beside the program, one folder each (languages/French).
The game reads a pack's files in place of the disc's own (hle_dvd.c), as
Options > Gameplay > Language and Voices say.

The USA program already knows Europe's languages: its table of file
endings has _Jp, _Us, _En, _Sp, _It, _Fr and _Ge, and the European disc is
the same game with each language's files beside the English ones. So a
pack is that disc's files of one language under the USA names, which the
USA game then reads as its own:

  text/files    Message/EU_<language>/* as Message/US_English/*, and
                Archive/<name>_<ending>.dac and Hbm/Hbm_<ending>.dac
                as <name>.dac (the menus' words and pictures)
  voices/files  Sound/<name>_<ending>.acb and its _streamfiles.awb, and
                Movie/<name>_<ending>.mo, as <name>.acb, .awb and .mo
                (French and German only: Spanish and Italian have the
                English voices)

The European disc (S2LP01) makes French, German, Italian and Spanish (its
English is the USA's). The Japanese disc (S2LJ01) makes Japanese: its
voices are its own files of the names the European disc has French voices
for, and its text, kept elsewhere on that disc, is put into the USA files
(japanese_text, below).

The game makes the same packs itself (recomp/boot/langpack.c, from
Options > Gameplay > Language pack); this script is its reference.
"""
import os
import shutil
import struct
import sys

# The European disc's languages: pack name, file ending, message folder,
# and whether it has voices of its own.
EUROPE = [
    ("French", "_Fr", "EU_French", True),
    ("German", "_Ge", "EU_German", True),
    ("Italian", "_It", "EU_Italian", False),
    ("Spanish", "_Sp", "EU_Spanish", False),
]

# The files with voices in them: the European disc has French and German
# ones of each. The Japanese disc's files of these names are its voices.
VOICE_SOUNDS = [
    "M_AT_01", "M_MIJU_AT_VOICE", "M_POKA_AT_VOICE", "M_TUTA_AT_VOICE", "S_DEMO_COMMON", "S_Fd0101", "S_Fd0102",
    "S_Fd0301", "S_Fd0302", "S_Fd0303", "S_Fd0401", "S_Fd0402", "S_Fd0501", "S_Fd0502", "S_Fd0503", "S_Fd0601",
    "S_Fd0605", "S_Fd0801", "S_MIJU_VOICE", "S_POKA_VOICE", "S_POKE_VOICE", "S_RICH_COMMON", "S_TUTA_VOICE",
]
VOICE_MOVIES = ["DmCh1110_01", "DmCh1130_00", "DmCh4180_01", "DmCh5300_00", "DmCh5400_00", "DmCh5410_00"]


def disc_id(files):
    try:
        with open(os.path.join(os.path.dirname(os.path.normpath(files)), "sys", "boot.bin"), "rb") as f:
            return f.read(6).decode("ascii", "replace")
    except OSError:
        return ""


def copy(src, dst, log):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    log.append(dst)


def with_ending(path, ending):
    """Sound/S_POKE_VOICE.acb -> Sound/S_POKE_VOICE_Fr.acb; the AWB's ending
    goes before _streamfiles (S_POKE_VOICE_Fr_streamfiles.awb)."""
    d, name = os.path.split(path)
    if name.endswith("_streamfiles.awb"):
        return os.path.join(d, name[: -len("_streamfiles.awb")] + ending + "_streamfiles.awb")
    stem, ext = os.path.splitext(name)
    return os.path.join(d, stem + ending + ext)


def usa_files(usa, folder):
    root = os.path.join(usa, folder)
    return sorted(n for n in os.listdir(root) if os.path.isfile(os.path.join(root, n))) if os.path.isdir(root) else []


def voice_paths():
    for s in VOICE_SOUNDS:
        yield "Sound/" + s + ".acb"
        yield "Sound/" + s + "_streamfiles.awb"
    for m in VOICE_MOVIES:
        yield "Movie/" + m + ".mo"


def europe(usa, other, out, name, ending, folder, voices):
    text, voice = [], []
    pack = os.path.join(out, name)
    for n in usa_files(usa, "Message/US_English"):
        src = os.path.join(other, "Message", folder, n)
        if os.path.isfile(src):
            copy(src, os.path.join(pack, "text", "files", "Message", "US_English", n), text)
    for sub in ("Archive", "Hbm"):
        for n in usa_files(usa, sub):
            src = os.path.join(other, with_ending(os.path.join(sub, n), ending))
            if os.path.isfile(src):
                copy(src, os.path.join(pack, "text", "files", sub, n), text)
    if voices:
        for p in voice_paths():
            src = os.path.join(other, with_ending(p, ending))
            if os.path.isfile(src) and os.path.isfile(os.path.join(usa, p)):
                copy(src, os.path.join(pack, "voices", "files", p), voice)
    return text, voice


# ---------------------------------------------------------------------------
# The Japanese text. The Japanese disc has the same messages as the USA one
# (the same labels in nearly every file), but kept elsewhere: each area's in
# its Field/<area>Res.dac (in Temporary/Field/Park<area>Res.dan), the menus'
# under Message/JP_Kanji/ in their archives, and the shared ones in
# Archive/ResidentMessage_JP.dac (Kanji/ for those with kanji versions).
# Each message file has two versions, kana only (JP_Japanese) and with
# kanji (JP_Kanji); the pack takes the kanji one where there is one.
#
# The pack keeps the USA files and archives and puts the Japanese text in
# them, message by message by label: a message the Japanese file lacks
# keeps its English, and each file keeps the USA one's other sections
# (attributes and the like), which the USA program reads. The font is the
# Japanese one under the USA font's name. Kept English: the staff roll
# (a different list, with its own table) and the nicknames (the Japanese
# game makes them from two halves, WORD1 and WORD2, and the USA one from a
# list of whole names, WORD_nn_nn, which the Japanese file doesn't have).

JAPANESE_KEEP = ("NickName.msbt", "StaffRoll.msbt")


def tpl_shape(t):
    """A TPL's pictures: width, height and format of each, and its palette's
    format; None if it isn't one."""
    if t[:4] != b"\x00\x20\xAF\x30":
        return None
    count, table = struct.unpack_from(">II", t, 4)
    shape = []
    for i in range(count):
        image, palette = struct.unpack_from(">II", t, table + 8 * i)
        height, width, fmt = struct.unpack_from(">HHI", t, image)
        shape.append((width, height, fmt, struct.unpack_from(">I", t, palette + 4)[0] if palette else None))
    return shape


def tpl_fits(usa, theirs):
    """Whether a picture can take the other disc's place: the same sizes and
    formats (the USA menus' layouts are drawn for them). A palette may have
    more or fewer colours: the game reads its size from the file."""
    a, b = tpl_shape(usa), tpl_shape(theirs)
    return a is not None and a == b


# Words in pictures the Japanese disc draws smaller (its words are
# shorter): each is set in a clear picture of the USA one's size, so the
# USA layouts show it at its own scale, at the left or the middle.
PAD_LEFT = ("ListText", "CmListText")
PAD_MIDDLE = ("ActionText", "ButtonNoText", "ButtonYesText", "ButtonBibText", "ButtonSpeedText", "KeyTextView")


def tpl_pad(name, usa, theirs):
    """The other disc's picture in a clear one of the USA one's size, or None
    if it isn't one of those (8-bit, one picture, no bigger)."""
    a, b = tpl_shape(usa), tpl_shape(theirs)
    if not a or not b or len(a) != 1 or len(b) != 1:
        return None
    (W, H, f, pf), (w, h, g, pg) = a[0], b[0]
    left = name.startswith(PAD_LEFT)
    if not (left or name.startswith(PAD_MIDDLE)) or f != g or f not in (2, 9) or pf != pg:
        return None
    if w > W or h > H or (w, h) == (W, H):
        return None
    image, palette = struct.unpack_from(">II", theirs, struct.unpack_from(">I", theirs, 8)[0])
    data = struct.unpack_from(">I", theirs, image + 8)[0]
    clear = 0  # IA4: no alpha
    if f == 9:  # C8: a clear colour of its RGB5A3 palette
        count, = struct.unpack_from(">H", theirs, palette)
        pfmt, pdata = struct.unpack_from(">II", theirs, palette + 4)
        clear = next((i for i in range(count) if pfmt == 2 and
                      not struct.unpack_from(">H", theirs, pdata + 2 * i)[0] & 0xF000), None)
        if clear is None:
            return None
    pixels = bytearray([clear]) * (((W + 7) // 8) * ((H + 3) // 4) * 32)
    x0, y0 = 0 if left else (W - w) // 2, (H - h) // 2
    for y in range(h):
        for x in range(w):
            X, Y = x0 + x, y0 + y
            pixels[((Y // 4) * ((W + 7) // 8) + X // 8) * 32 + (Y % 4) * 8 + X % 8] =                 theirs[data + ((y // 4) * ((w + 7) // 8) + x // 8) * 32 + (y % 4) * 8 + x % 8]
    out = bytearray(theirs)
    out += bytes(-len(out) % 32)
    struct.pack_into(">HH", out, image, H, W)
    struct.pack_into(">I", out, image + 8, len(out))
    return bytes(out + pixels)


def lz11_decompress(d):
    if d[:1] != b"\x11":
        return d
    size, p = d[1] | d[2] << 8 | d[3] << 16, 4
    if size == 0:
        size, p = struct.unpack_from("<I", d, 4)[0], 8
    out = bytearray()
    while len(out) < size:
        flags = d[p]
        p += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if not flags & (0x80 >> bit):
                out.append(d[p])
                p += 1
                continue
            b = d[p]
            if b >> 4 == 0:
                n, disp = ((b & 15) << 4 | d[p + 1] >> 4) + 0x11, ((d[p + 1] & 15) << 8 | d[p + 2]) + 1
                p += 3
            elif b >> 4 == 1:
                n = ((b & 15) << 12 | d[p + 1] << 4 | d[p + 2] >> 4) + 0x111
                disp = ((d[p + 2] & 15) << 8 | d[p + 3]) + 1
                p += 4
            else:
                n, disp = (b >> 4) + 1, ((b & 15) << 8 | d[p + 1]) + 1
                p += 2
            for _ in range(n):
                out.append(out[-disp])
    return bytes(out)


def hash3(d, i):
    """langpack.c's hash of three bytes: the chains it searches are of
    these, not of the bytes themselves."""
    return ((d[i] << 16 | d[i + 1] << 8 | d[i + 2]) * 2654435761 & 0xFFFFFFFF) >> 16


def lz11_compress(d):
    """Greedy LZ11 with hash chains over the 4 KB window, as langpack.c
    does (the same bytes)."""
    n = len(d)
    out = bytearray([0x11, n & 255, n >> 8 & 255, n >> 16 & 255]) if n < 1 << 24 else \
        bytearray([0x11, 0, 0, 0]) + struct.pack("<I", n)
    head, prev = {}, [-1] * n
    i, bit, flag_pos = 0, 8, 0
    while i < n:
        if bit == 8:
            flag_pos = len(out)
            out.append(0)
            bit = 0
        best_len = best_disp = 0
        if i + 3 <= n:
            cand = head.get(hash3(d, i), -1)
            limit = min(n - i, 0x10110)
            tries = 48
            while cand >= 0 and i - cand <= 0x1000 and tries:
                tries -= 1
                k = 0
                while k < limit and d[cand + k] == d[i + k]:
                    k += 1
                if k > best_len:
                    best_len, best_disp = k, i - cand
                    if k == limit:
                        break
                cand = prev[cand]
        if best_len >= 3:
            disp = best_disp - 1
            out[flag_pos] |= 0x80 >> bit
            if best_len <= 0x10:
                out += bytes([(best_len - 1) << 4 | disp >> 8, disp & 255])
            elif best_len <= 0x110:
                m = best_len - 0x11
                out += bytes([m >> 4, (m & 15) << 4 | disp >> 8, disp & 255])
            else:
                m = best_len - 0x111
                out += bytes([0x10 | m >> 12, m >> 4 & 255, (m & 15) << 4 | disp >> 8, disp & 255])
            step = best_len
        else:
            out.append(d[i])
            step = 1
        for q in range(i, i + step):
            if q + 3 <= n:
                key = hash3(d, q)
                prev[q] = head.get(key, -1)
                head[key] = q
        i += step
        bit += 1
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def u8_files(d):
    """An archive's files: (path, node, offset, size) of each, in node order."""
    root = struct.unpack_from(">I", d, 4)[0]
    total = struct.unpack_from(">I", d, root + 8)[0]
    strs = root + total * 12
    files, names, ends = [], [], []
    for i in range(1, total):
        while ends and i >= ends[-1]:
            ends.pop()
            names.pop()
        t, off, size = struct.unpack_from(">III", d, root + i * 12)
        name = d[strs + (t & 0xFFFFFF):d.index(b"\0", strs + (t & 0xFFFFFF))].decode()
        if t >> 24:
            ends.append(size)
            names.append(name)
        else:
            files.append(("/".join([n for n in names if n and n != "."] + [name]), i, off, size))
    return files


def u8_replace(d, new):
    """The archive with new[path] in place of those files: the header and
    the node and name tables kept, the data laid out again in its old
    order, each file on 32 bytes (as langpack.c does)."""
    root, _, start = struct.unpack_from(">III", d, 4)
    out = bytearray(d[:start])
    for path, node, off, size in sorted(u8_files(d), key=lambda f: f[2]):
        body = new.get(path, d[off:off + size])
        out += bytes(-len(out) % 32)
        struct.pack_into(">II", out, root + node * 12 + 4, len(out), len(body))
        out += body
    out += bytes(-len(out) % 32)
    return bytes(out)


def u8_members(d):
    return dict((p, d[o:o + n]) for p, _, o, n in u8_files(d))


def msbt_sections(d):
    bom = ">" if d[8:10] == b"\xfe\xff" else "<"
    count = struct.unpack_from(bom + "H", d, 0x0E)[0]
    secs, p = [], 0x20
    for _ in range(count):
        size = struct.unpack_from(bom + "I", d, p + 4)[0]
        secs.append([d[p:p + 4], d[p + 8:p + 16], d[p + 16:p + 16 + size]])
        p = (p + 16 + size + 15) & ~15
    return bom, secs


def msbt_labels(bom, secs):
    for magic, _, s in secs:
        if magic == b"LBL1":
            labels = {}
            for i in range(struct.unpack_from(bom + "I", s, 0)[0]):
                count, off = struct.unpack_from(bom + "II", s, 4 + 8 * i)
                for _ in range(count):
                    n = s[off]
                    labels[s[off + 1:off + 1 + n].decode()] = struct.unpack_from(bom + "I", s, off + 1 + n)[0]
                    off += 5 + n
            return labels
    return {}


def msbt_texts(bom, secs):
    for magic, _, s in secs:
        if magic == b"TXT2":
            n = struct.unpack_from(bom + "I", s, 0)[0]
            offs = [struct.unpack_from(bom + "I", s, 4 + 4 * i)[0] for i in range(n)] + [len(s)]
            return [s[offs[i]:offs[i + 1]] for i in range(n)]
    return []


def msbt_merge(usa, theirs):
    """The USA message file with each message the other file has (by label)
    in its place; how many were taken."""
    bom, secs = msbt_sections(usa)
    obom, osecs = msbt_sections(theirs)
    if bom != obom:
        return usa, 0
    labels, olabels = msbt_labels(bom, secs), msbt_labels(obom, osecs)
    texts, otexts = msbt_texts(bom, secs), msbt_texts(obom, osecs)
    taken = 0
    for label, i in labels.items():
        j = olabels.get(label)
        if j is not None and i < len(texts) and j < len(otexts):
            texts[i] = otexts[j]
            taken += 1
    body = struct.pack(bom + "I", len(texts))
    pos = 4 + 4 * len(texts)
    for t in texts:
        body += struct.pack(bom + "I", pos)
        pos += len(t)
    body += b"".join(texts)
    out = bytearray(usa[:0x20])
    for magic, rest, s in secs:
        if magic == b"TXT2":
            s = body
        out += magic + struct.pack(bom + "I", len(s)) + rest + s
        while len(out) % 16:
            out.append(0xAB)
    struct.pack_into(bom + "I", out, 0x12, len(out))
    return bytes(out), taken


def archive_members(path):
    with open(path, "rb") as f:
        return u8_members(lz11_decompress(f.read()))


def japanese_text(usa, other, out):
    """The Japanese text pack's files (written to out/Japanese/text/files);
    the paths written and how many messages are Japanese."""
    files = os.path.join(out, "Japanese", "text", "files")
    written, messages = [], 0

    def put(rel, data):
        dst = os.path.join(files, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(data)
        written.append(rel)

    def merge(name, usa_data, kanji, kana):
        nonlocal messages
        if name in JAPANESE_KEEP:
            return usa_data
        theirs = kanji if kanji is not None else kana
        if theirs is None:
            return usa_data
        data, taken = msbt_merge(usa_data, theirs)
        messages += taken
        return data

    def rebuild(rel, replace):
        """USA archive rel with replace(path, data) -> data for each file."""
        with open(os.path.join(usa, rel), "rb") as f:
            d = lz11_decompress(f.read())
        new = {}
        for path, data in u8_members(d).items():
            data2 = replace(path, data)
            if data2 != data:
                new[path] = data2
        if new:
            put(rel, lz11_compress(u8_replace(d, new)))

    # The areas' messages.
    for n in usa_files(usa, "Message/US_English"):
        area = n[:6]
        arc = os.path.join(other, "Field", area + "Res.dac")
        if not os.path.isfile(arc):
            continue
        dan = archive_members(arc).get("Temporary/Field/Park%sRes.dan" % area)
        if dan is None:
            continue
        inner = u8_members(dan)
        with open(os.path.join(usa, "Message", "US_English", n), "rb") as f:
            mine = f.read()
        data = merge(n, mine, inner.get("Message/JP_Kanji/" + n), inner.get("Message/JP_Japanese/" + n))
        if data != mine:
            put("Message/US_English/" + n, data)

    # The archives: their messages, the font, and the pictures with words.
    for n in usa_files(usa, "Archive"):
        src = os.path.join(other, "Archive", n)
        if not os.path.isfile(src):
            src = os.path.join(other, "Archive", n[:-4] + "_JP" + n[-4:])
        if not os.path.isfile(src):
            continue
        theirs = archive_members(src)

        def replace(p, d, t=theirs):
            folder, _, name = p.rpartition("/")
            if p == "Font/Standerd_US_EU.brfnt":
                return t.get("Font/Standerd_JP.brfnt", d)
            if name.endswith(".msbt"):
                if folder == "Message":
                    return merge(name, d, t.get("Message/JP_Kanji/" + name), t.get("Message/JP_Japanese/" + name))
                if not folder:
                    return merge(name, d, t.get("Kanji/" + name), t.get(name))
            if name.endswith(".tpl") and p in t:
                if tpl_fits(d, t[p]):
                    return t[p]
                return tpl_pad(name, d, t[p]) or d
            return d
        rebuild("Archive/" + n, replace)
    return written, messages


def japan(usa, other, out):
    voice = []
    for p in voice_paths():
        src = os.path.join(other, p)
        if os.path.isfile(src) and os.path.isfile(os.path.join(usa, p)):
            copy(src, os.path.join(out, "Japanese", "voices", "files", p), voice)
    return voice


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    usa, other, out = sys.argv[1:4]
    if not os.path.isdir(os.path.join(usa, "Message", "US_English")):
        print("%s isn't the USA disc's files folder (no Message/US_English)" % usa)
        return 1
    which = disc_id(other)
    if which.startswith("S2LP") or os.path.isdir(os.path.join(other, "Message", "EU_French")):
        for name, ending, folder, voices in EUROPE:
            shutil.rmtree(os.path.join(out, name), ignore_errors=True)
            text, voice = europe(usa, other, out, name, ending, folder, voices)
            print("%s: %d text files, %d voice files" % (name, len(text), len(voice)))
    elif which.startswith("S2LJ"):
        shutil.rmtree(os.path.join(out, "Japanese"), ignore_errors=True)
        text, messages = japanese_text(usa, other, out)
        print("Japanese: %d text files (%d messages), %d voice files" % (len(text), messages, len(japan(usa, other, out))))
    else:
        print("%s is not a Japanese (S2LJ01) or European (S2LP01) disc" % (which or other))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
