#!/usr/bin/env python3
"""Give Real Steel's later DLC parts their real names and descriptions.

Real Steel's DLC add-ons each ship a string table (pac/string.pac). The game
loads one: from the newest add-on whose table is in the original "SDB " format
(add-on 10/11.x). Add-ons 14-21 ship newer "SDBP"/"SDB2" tables that only the
game's title update reads, so the parts those add-ons sell show placeholders
such as "Text10900": the IDs exist in the old table, with "Text<id>" (English)
or empty (Japanese) text.

This tool keeps the old table's exact structure - every section, sub-file,
string ID and order - and fills only those placeholder IDs with the newer
table's text for the same ID, in every language the newer table has (English
and Japanese; the same IDs are "Text<id>" in English and empty in Japanese).
Learned the hard way, in-game:
  - Every section keeps its original size and sector: the game locates
    sections by their original positions, so resizing one moves the sections
    after it out from under the game (no text at all, or a crash).
  - Only the "56xx" sections, which hold the interface text, are touched.
  - Whole sections from the newer table cannot be swapped in: the title update
    renumbered strings the base code depends on.
  - Other empty entries stay empty: they hide title-update features (KO
    matches, ...) and filling them makes the base game try to use them.
  - The game decodes each sub-file into a buffer the original size fits, so
    identical strings are stored once (the original never shared them, which
    leaves room for the new text), and the BPE encoder keeps the decoder's
    expansion stack within what the original data needs.

Format notes (all little-endian):
  EPAC archive: "EPAC", header size, data size, 7; index at 0x800:
    "SDB " + slot count + 0, then slots of (4-char section id, sector, n);
    section data at 0x4000 + sector * 0x800. The last digit of a section id
    is the language.
  Section: "PACH", sub-file count, then (index, offset, size) per sub-file,
    sub-files 4-byte aligned after the table.
  Sub-file: "BPE ", 0x100, compressed size, decompressed size, then byte-pair
    encoded data (Gage's format, block length little-endian).
  Decoded sub-file: 0, entry count, entries (string offset, length, id,
    flags), then NUL-terminated strings.

usage: merge_string_tables.py <old SDB table> <newer table> <output>
"""
import struct
import sys

INDEX_OFFSET = 0x800
DATA_OFFSET = 0x4000
SECTOR = 0x800
BPE_BLOCK = 2048  # matches the original encoder's ratio (~3 KB blocks)
# The game's decoder expands nested pairs on a small fixed stack (the classic
# implementation uses 30 entries); a deeper pair overflows it and corrupts game
# memory. The original tables peak at ~21; stay below that.
BPE_MAX_STACK = 20
# Interface text sections (the others hold copies the game must find unchanged).
TEXT_SECTION_PREFIX = b"56"
# Precaution: filled descriptions are cut to the longest description the base
# game itself has in that language (characters and bytes), at a word boundary.
DESCRIPTION_PREFIXES = (b"(Purchased", b"(Default", b"(Initial")


# --- byte-pair encoding -------------------------------------------------------------------------

def bpe_expand(buf, dsize):
    out = bytearray()
    p = 0
    while p < len(buf) and len(out) < dsize:
        left = list(range(256))
        right = [0] * 256
        c = 0
        while True:
            count = buf[p]
            p += 1
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            for _ in range(count + 1):
                left[c] = buf[p]
                p += 1
                if left[c] != c:
                    right[c] = buf[p]
                    p += 1
                c += 1
            if c == 256:
                break
        size = buf[p] | (buf[p + 1] << 8)
        p += 2
        stack = []
        while True:
            if stack:
                c = stack.pop()
            else:
                if size == 0:
                    break
                size -= 1
                c = buf[p]
                p += 1
            if c == left[c]:
                out.append(c)
            else:
                stack.append(right[c])
                stack.append(left[c])
    if len(out) != dsize:
        raise ValueError(f"BPE expanded {len(out)} bytes, expected {dsize}")
    return bytes(out)


def _bpe_block(block):
    data = list(block)
    left = list(range(256))
    right = [0] * 256
    # Decoder stack entries needed to expand each code (a literal needs 1).
    need = [1] * 256
    used = set(data)
    while True:
        free = next((c for c in range(256) if c not in used), None)
        if free is None:
            break
        counts = {}
        for a, b in zip(data, data[1:]):
            counts[(a, b)] = counts.get((a, b), 0) + 1
        best = None
        for (a, b), n in sorted(counts.items(), key=lambda kv: -kv[1]):
            if n < 3:
                break
            if max(1 + need[a], need[b]) <= BPE_MAX_STACK:
                best = (a, b)
                break
        if best is None:
            break
        a, b = best
        need[free] = max(1 + need[a], need[b])
        out = []
        i = 0
        while i < len(data):
            if i + 1 < len(data) and data[i] == a and data[i + 1] == b:
                out.append(free)
                i += 2
            else:
                out.append(data[i])
                i += 1
        data = out
        left[free], right[free] = a, b
        used.add(free)
    # Pair table: a skip byte (127 + n) passes n literal codes and is followed
    # by exactly one explicit entry; otherwise a count byte (n - 1) is
    # followed by n explicit entries.
    table = bytearray()
    c = 0
    while c < 256:
        skip = 0
        while c + skip < 256 and left[c + skip] == c + skip and skip < 128:
            skip += 1
        if skip:
            table.append(127 + skip)
            c += skip
            if c == 256:
                break
            run = 1
        else:
            run = 0
            while c + run < 256 and left[c + run] != c + run and run < 128:
                run += 1
            table.append(run - 1)
        for code in range(c, c + run):
            table.append(left[code])
            if left[code] != code:
                table.append(right[code])
        c += run
    return bytes(table) + struct.pack("<H", len(data)) + bytes(data)


def bpe_compress(raw):
    return b"".join(_bpe_block(raw[i:i + BPE_BLOCK]) for i in range(0, len(raw), BPE_BLOCK))


# --- containers ---------------------------------------------------------------------------------

def read_table(path):
    data = open(path, "rb").read()
    if data[:4] != b"EPAC":
        raise SystemExit(f"{path}: not an EPAC archive")
    magic = data[INDEX_OFFSET:INDEX_OFFSET + 4]
    if magic not in (b"SDB ", b"SDBP", b"SDB2"):
        raise SystemExit(f"{path}: unknown string database {magic!r}")
    slots = struct.unpack_from("<I", data, INDEX_OFFSET + 4)[0]
    entries = []
    pos = INDEX_OFFSET + 12
    for _ in range(slots):
        gid = data[pos:pos + 4]
        sector, count = struct.unpack_from("<II", data, pos + 4)
        pos += 12
        if gid != b"\0\0\0\0":
            entries.append((gid, sector, count))
    starts = sorted({e[1] for e in entries})
    end_sector = (len(data) - DATA_OFFSET) // SECTOR
    sections = []
    for gid, sector, count in entries:
        later = [s for s in starts if s > sector]
        stop = later[0] if later else end_sector
        blob = data[DATA_OFFSET + sector * SECTOR:DATA_OFFSET + stop * SECTOR]
        if blob[:4] != b"PACH":
            raise SystemExit(f"{path}: section {gid!r} does not start with PACH")
        sections.append([gid, count, blob])
    return magic, slots, data, sections


def read_pach(blob):
    n = struct.unpack_from("<I", blob, 4)[0]
    header = 8 + 12 * n
    subs = []
    for i in range(n):
        index, offset, size = struct.unpack_from("<III", blob, 8 + 12 * i)
        subs.append((index, blob[header + offset:header + offset + size]))
    return subs


def write_pach(subs):
    header = bytearray(b"PACH" + struct.pack("<I", len(subs)))
    body = bytearray()
    for index, sub in subs:
        header += struct.pack("<III", index, len(body), len(sub))
        body += sub
        body += b"\0" * (-len(body) % 4)
    blob = header + body
    return bytes(blob + b"\0" * (-len(blob) % SECTOR))


def decode_sub(sub):
    if sub[:4] != b"BPE ":
        raise ValueError("sub-file is not BPE")
    _, csize, dsize = struct.unpack_from("<III", sub, 4)
    raw = bpe_expand(sub[16:16 + csize], dsize)
    _, n = struct.unpack_from("<II", raw, 0)
    strings = []
    for i in range(n):
        off, length, sid, flags = struct.unpack_from("<IIII", raw, 8 + 16 * i)
        strings.append([sid, flags, raw[off:off + length]])
    return raw, strings


def encode_sub(strings, share=False):
    """share=True stores each distinct string once (entries are offset+length,
    so they can point at the same text). The game decompresses each sub-file
    into a fixed-size buffer and shows no text at all if it does not fit; the
    original tables never shared strings, so sharing leaves room for the
    filled-in DLC names."""
    table = bytearray(struct.pack("<II", 0, len(strings)))
    text = bytearray()
    base = 8 + 16 * len(strings)
    seen = {}
    for sid, flags, s in strings:
        if share and s in seen:
            offset = seen[s]
        else:
            offset = base + len(text)
            seen[s] = offset
            text += s
        table += struct.pack("<IIII", offset, len(s), sid, flags)
    raw = bytes(table + text)
    packed = bpe_compress(raw)
    return b"BPE " + struct.pack("<III", 0x100, len(packed), len(raw)) + packed, raw


def strings_by_id(sections, languages):
    found = {}
    for gid, _, blob in sections:
        if gid[3:4] not in languages:
            continue
        for _, sub in read_pach(blob):
            _, strings = decode_sub(sub)
            for sid, _, s in strings:
                found[(gid[3:4], sid)] = s
    return found


def is_empty(s):
    return s.strip(b"\0") == b""


def fit(text, max_chars, max_bytes):
    """Shorten NUL-terminated UTF-8 text to max_chars characters and max_bytes
    bytes (both including the terminator)."""
    body = text.rstrip(b"\0").decode("utf-8", "replace")
    if len(body) + 1 <= max_chars and len(body.encode()) + 1 <= max_bytes:
        return text
    cut = body
    while cut and (len(cut) + 4 > max_chars or len(cut.encode()) + 4 > max_bytes):
        cut = cut[:-1]
    if " " in cut[len(cut) // 2:]:
        cut = cut[:cut.rindex(" ")]
    return cut.rstrip(" ,;").encode() + b"...\0"


def description_limits(sections):
    """Per language: (max chars, max bytes) over the old table's part
    descriptions, identified by their English text."""
    english = strings_by_id(sections, {b"0"})
    ids = {sid for (_, sid), t in english.items() if t.startswith(DESCRIPTION_PREFIXES)}
    limits = {}
    for gid, _, blob in sections:
        lang = gid[3:4]
        for _, sub in read_pach(blob):
            for sid, _, t in decode_sub(sub)[1]:
                if sid in ids and not is_placeholder(sid, t):
                    chars = len(t.rstrip(b"\0").decode("utf-8", "replace")) + 1
                    old = limits.get(lang, (0, 0))
                    limits[lang] = (max(old[0], chars), max(old[1], len(t)))
    return limits


def is_placeholder(sid, s):
    """The old table marks DLC strings it has no text for as "Text<id>"."""
    return s.rstrip(b"\0") == b"Text%d" % sid


def placeholder_ids(sections, newer):
    """IDs the old table shows as "Text<id>" in some language and the newer
    table has real text for. Only these are filled (see the module notes)."""
    ids = set()
    for gid, _, blob in sections:
        for _, sub in read_pach(blob):
            _, strings = decode_sub(sub)
            for sid, _, s in strings:
                if not is_placeholder(sid, s):
                    continue
                for (lang, nsid), text in newer.items():
                    if nsid == sid and not is_empty(text) and not text.startswith(b"Text"):
                        ids.add(sid)
                        break
    return ids


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    old_path, new_path, out_path = sys.argv[1:]
    old_magic, slots, old_data, old_sections = read_table(old_path)
    _, _, _, new_sections = read_table(new_path)
    if old_magic != b"SDB ":
        raise SystemExit(f"{old_path}: must be an original 'SDB ' table")
    languages = {gid[3:4] for gid, _, _ in new_sections}
    newer = strings_by_id(new_sections, languages)
    wanted = placeholder_ids(old_sections, newer)
    limits = description_limits(old_sections)

    filled = 0
    out_blobs = []
    for gid, count, blob in old_sections:
        lang = gid[3:4]
        if lang not in languages or not gid.startswith(TEXT_SECTION_PREFIX):
            out_blobs.append(blob)
            continue
        subs = []
        changed = False
        for index, sub in read_pach(blob):
            _, strings = decode_sub(sub)
            touched = False
            for entry in strings:
                sid, _, s = entry
                replacement = newer.get((lang, sid))
                if (sid in wanted and (is_empty(s) or is_placeholder(sid, s))
                        and replacement is not None and not is_empty(replacement)):
                    max_chars, max_bytes = limits.get(lang, (1 << 30, 1 << 30))
                    entry[2] = fit(replacement, max_chars, max_bytes)
                    touched = True
                    filled += 1
            if touched:
                original_size = struct.unpack_from("<I", sub, 12)[0]
                sub, raw = encode_sub(strings, share=True)
                if decode_sub(sub)[0] != raw:
                    raise SystemExit("BPE round trip failed")
                if len(raw) > original_size:
                    raise SystemExit(f"section {gid!r} sub-file {index}: {len(raw)} bytes "
                                     f"decoded exceeds the original {original_size}; the "
                                     "game would reject it")
                changed = True
            subs.append((index, sub))
        if changed:
            rebuilt = write_pach(subs)
            if len(rebuilt) > len(blob):
                raise SystemExit(f"section {gid!r}: rebuilt section ({len(rebuilt)} bytes) "
                                 f"does not fit its original {len(blob)} bytes")
            # Every section must stay at its original sector: the game finds
            # sections by their original positions, and moving one (by
            # resizing an earlier section) leaves it unable to read the text.
            blob = rebuilt + b"\0" * (len(blob) - len(rebuilt))
        out_blobs.append(blob)

    out = bytearray(old_data[:DATA_OFFSET])
    index = bytearray(b"SDB " + struct.pack("<II", slots, 0))
    sector = 0
    for (gid, count, _), blob in zip(old_sections, out_blobs):
        index += gid + struct.pack("<II", sector, count)
        out += blob
        sector += len(blob) // SECTOR
    index += b"\0" * (12 * (slots - len(old_sections)))
    out[INDEX_OFFSET:INDEX_OFFSET + len(index)] = index
    struct.pack_into("<I", out, 8, len(out) - (DATA_OFFSET + SECTOR))
    open(out_path, "wb").write(bytes(out))
    print(f"{out_path}: filled {filled} placeholder strings ({len(wanted)} ids) from the newer table "
          f"(languages {sorted(l.decode() for l in languages)}), {len(out)} bytes")


if __name__ == "__main__":
    main()
