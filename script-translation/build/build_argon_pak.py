"""Build the game's script.pak from the reviewed Argon text and the Gemini translation of the missing packets.

Text: out/responses (104 packets, reviewed Argon) + gemini_translate/responses (33 packets) + the six da0202
lines the package had no cells for (gemini_translate/responses/da0202_missing6.json).
Base script: the official 1.10 script (khd-runtime-fix/jp120, source B). The packets' byte offsets point into
source A (khd-retl/JP_PURE); every A span is moved onto B through a character diff (38/119 files are equal).
Kept from the current game script: the replay-gallery hooks (CALC RP_*, the RPmode return jumps) and the menu
scripts the packets do not cover (dh_menu, dh_extra, dh_ex_cg) plus the non-script entries.
Also: the da0202 SE tag gets its missing closing quote (it swallowed the six lines above), omitted rubies lose
their tag, and Korean lines wider than their text box are word-wrapped at spaces with the same rules as
khd-runtime-fix/tools/restore_build.py (staged lines and lines with inline tags are left alone).
Packed with the fixed packer (tail bytes encrypted).

  python -I -X utf8 build_argon_pak.py <current game script.pak> <game 0system.pak> <out.pak> [--report r.json]
"""
import difflib
import importlib.util
import json
import re
import shutil
import sys
import unicodedata
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
spec = importlib.util.spec_from_file_location("knew", r"P:\AI\khd-runtime-fix\tools\kishinhisho_pak_fixed.py")
K = importlib.util.module_from_spec(spec)
sys.modules["knew"] = K
spec.loader.exec_module(K)

ROOT = Path(r"P:\AI\khd-argon-review")
PKG = ROOT / "pkg" / "KishinHisho_Claude_Review_20261031"
RESP_DIRS = [ROOT / "out" / "responses", ROOT / "gemini_translate" / "responses"]
SIDE = ROOT / "gemini_translate" / "responses" / "da0202_missing6.json"
SIDE_IN = ROOT / "gemini_translate" / "in_da0202_missing6.json"
SRC_A = Path(r"P:\AI\khd-retl\JP_PURE\script.pak")
SRC_B = Path(r"P:\AI\khd-runtime-fix\jp120\script.pak")
MAP = json.loads(Path(r"P:\AI\khd-retl\cp932_han_carrier_map_ext.json").read_text(encoding="utf-8"))["mappings"]
ENC = {x["target"]: x["carrier"] for x in MAP}
CARRIERS = set(ENC.values())
CHARFIX = {"\u2014": "\u2015", "\u00b7": "\u30fb", "\u2013": "\u2015", "\u2027": "\u30fb"}
TAGRE = re.compile(r'<(?:[^>"]|"[^"]*")*>')
TAGRE_B = re.compile(rb'<(?:[^>"]|"[^"]*")*>')
RUBYTAG = re.compile(r'(?i)</?(r|font)[\s>]')
RB = re.compile(r'<r text="([^"]*)">(.*?)</r>', re.I)
HEAD = re.compile(r'^[\u3000 ]*')
TAILTAG = re.compile(r'((?:<(?!/?r[\s>])(?:[^>"]|"[^"]*")*>)*)$', re.I)
RPRE = re.compile(r'<CALC\s+WORKNAME="RP[^"]*"[^>]*>|<A\s+HREF="dh_ex_story\.nps#RET"[^>]*>', re.I)
SE_FIX = ('SRC="seロボ特殊_汎用斬撃命中飛翔 MODE=', 'SRC="seロボ特殊_汎用斬撃命中飛翔" MODE=')   # 3 tags in da0202
JPCHAR = re.compile(r"[ぁ-ゖァ-ヺ一-鿿]")
SP = "\u3000"

report = {"conflicts": [], "untranslated": [], "unencodable": {}, "attr_fallback": [], "omit_fallback": [],
          "rp_unplaced": [], "side": [], "wrapped": 0, "wrap_skipped": {}, "jp_left": {}, "tag_check": [],
          "fallback_A": [], "ruby_split": [], "ruby_unsplit": [], "layout_fix": [], "stage_fix": []}


def load_pak(path):
    a = K.parse_archive(Path(path))
    return a, {e.name.rsplit("\\", 1)[-1].lower(): K.decode_stored(e, K.read_stored(a, e)) for e in a.entries}


def char_offsets(raw):
    offs, i, n = [], 0, len(raw)
    while i < n:
        offs.append(i)
        b = raw[i]
        i += 2 if (0x81 <= b <= 0x9F or 0xE0 <= b <= 0xFC) and i + 1 < n else 1
    offs.append(n)
    return offs


def enc(s):
    s = "".join(CHARFIX.get(c, c) for c in s).replace("\r\n", "\r").replace("\n", "\r")
    out = []
    for c in s:
        c2 = ENC.get(c, c)
        try:
            out.append(c2.encode("cp932"))
        except UnicodeEncodeError:
            report["unencodable"][c] = report["unencodable"].get(c, 0) + 1
            out.append(b"?")
    return b"".join(out)


def visible(s):
    return re.sub(TAGRE, "", RB.sub(lambda m: m.group(2), s))


# 11-05 line width as the engine measures it (system.dll 0x5b770): each character's GetTextExtentPoint32A on the
# message font (system.ini fontSize=24 -> em 24 px; KHD Korean: Hangul 920/1000 = 22 px, U+3000 and 「」… 24 px,
# ASCII space 5 px, comma 7 px), and a character fits only while x + width < box width. The 11-01 builds counted
# every wide character as 24 px and every narrow one as 12, so lines that fit were folded (da0120ed 3 lines).
METRICS = "old" if "--metrics" in sys.argv and sys.argv[sys.argv.index("--metrics") + 1] == "old" else "engine"
FONT = Path(r"F:\AI\khd-latest-game\KHDKorean-CP932Han-Ext.otf")
_FONT = []


def char_px(c):
    if not _FONT:
        from fontTools.ttLib import TTFont
        f = TTFont(str(FONT))
        _FONT.extend([f.getBestCmap(), f["hmtx"], f["head"].unitsPerEm])
    cmap, hmtx, upm = _FONT
    g = cmap.get(ord(c))
    if g is None:
        return 24 if unicodedata.east_asian_width(c) in "FWA" else 12
    return int(hmtx[g][0] * 24 / upm + 0.5)


def width(s):
    if METRICS == "old":
        return sum(1.0 if unicodedata.east_asian_width(c) in "FWA" else 0.5 for c in visible(s))
    return sum(char_px(c) for c in visible(s)) / 24


WORD = re.compile(r'(?:<r\s+text="[^"]*">.*?</r>|<(?:[^>"]|"[^"]*")*>|[^ <])+', re.I)   # rubies and tags stay whole
RUBY_ONE = re.compile(r'(<r\s+text=")([^"]*)(">)(.*?)(</r>)', re.I)
DOTSET = set("・･ 　")


def split_reading(reading, widths):
    """cut a ruby reading in len(widths) pieces that follow the base pieces: at its spaces when it has enough, else by
    characters"""
    total = sum(widths)
    words = re.findall(r"[^ 　]+", reading)
    if len(words) >= len(widths):
        bounds, acc = [], 0
        for w in widths[:-1]:
            acc += w
            bounds.append(acc / total)
        lens = [len(x) for x in words]
        tot, acc, k, out = sum(lens), 0, 0, [[] for _ in widths]
        for wd, ln in zip(words, lens):
            while k < len(bounds) and (acc + ln / 2) / tot > bounds[k]:
                k += 1
            out[k].append(wd)
            acc += ln
        for i in range(len(out)):          # no empty piece
            if not out[i]:
                j = i - 1 if i and len(out[i - 1]) > 1 else i + 1
                if 0 <= j < len(out) and len(out[j]) > 1:
                    out[i].append(out[j].pop(-1 if j < i else 0))
        return [" ".join(x) for x in out]
    chars, out, start, acc = list(reading.strip()), [], 0, 0
    for i, w in enumerate(widths):
        acc += w
        end = len(chars) if i == len(widths) - 1 else round(len(chars) * acc / total)
        out.append("".join(chars[start:end]).strip())
        start = end
    return out


def split_wide_rubies(name, core, maxw):
    """a ruby wider than the line cannot be folded by the engine without tearing (da9903 R'lyeh chant spread over
    three windows): cut its base at spaces into pieces that fit, and the reading with it"""
    def rep(m):
        o1, reading, o3, base, close = m.groups()
        if width(base) <= maxw:
            return m.group(0)
        parts, cur = [], ""
        for w in base.split(" "):
            cand = w if not cur else cur + " " + w
            if not cur or width(cand) <= maxw:
                cur = cand
            else:
                parts.append(cur)
                cur = w
        if cur:
            parts.append(cur)
        if len(parts) == 1:
            report["ruby_unsplit"].append((name, base[:30]))
            return m.group(0)
        pw = [width(p) for p in parts]
        if set(reading) <= DOTSET:
            rp = ["・" * len(re.sub(r"[ 　]", "", p)) for p in parts]
        else:
            rp = split_reading(reading, pw)
        report["ruby_split"].append((name, base[:24], len(parts)))
        return " ".join(f"{o1}{r}{o3}{p}{close}" for r, p in zip(rp, parts))
    return RUBY_ONE.sub(rep, core)


def wrap(text, first_avail, next_avail):
    """greedy word wrap at ASCII spaces; rubies and tags never broken (restore_build.py)"""
    words = WORD.findall(text)
    lines, cur, avail = [], "", first_avail
    for w in words:
        cand = w if not cur else cur + " " + w
        if not cur or width(cand) <= avail:
            cur = cand
        else:
            parts = cur.split(" ")
            if (len(parts) >= 2 and len(parts[-1]) == 1 and parts[-1] in CARRIERS
                    and width(parts[-1] + " " + w) <= next_avail):
                lines.append(" ".join(parts[:-1]))
                cur, avail = parts[-1] + " " + w, next_avail
            else:
                lines.append(cur)
                cur, avail = w, next_avail
    if cur:
        lines.append(cur)
    if len(lines) >= 2 and width(lines[-1]) <= 3.5:
        prev = WORD.findall(lines[-2])
        if len(prev) >= 2:
            moved = prev[-1] + " " + lines[-1]
            if width(moved) <= next_avail:
                lines[-2] = " ".join(prev[:-1])
                lines[-1] = moved
    return lines


def box_caps(system_pak):
    a = K.parse_archive(Path(system_pak))
    e = [x for x in a.entries if x.name.lower().endswith("textbox.ini")][0]
    t = K.decode_stored(e, K.read_stored(a, e)).decode("cp932", "replace")
    caps = {}
    for blk in re.split(r"(?=\[BOX\d+\])", t):
        m = re.match(r"\s*\[BOX(\d+)\]", blk)
        if not m:
            continue
        kv = {k.lower(): int(v) for k, v in re.findall(r"(?im)^\s*(\w+)\s*=\s*(-?\d+)", blk)}
        if "boxwidth" in kv:
            strict = 1 if METRICS == "engine" else 0      # the last character must end before the edge
            caps[int(m.group(1))] = (kv["boxwidth"] - kv.get("marginleft", 0) - kv.get("marginright", 0) - strict) / 24
    return caps


def load_text():
    """{file: [(a_start, a_end, kind, ko, sid, ruby_omit, base_end)]} from packets + responses"""
    resp = {}
    for d in RESP_DIRS:
        for f in d.glob("P*.json"):
            resp[f.stem] = {r["id"]: r for r in json.loads(f.read_text(encoding="utf-8"))["records"]}
    per = {}
    for p in sorted((PKG / "packets").glob("*.json")):
        pkt = json.loads(p.read_text(encoding="utf-8"))
        rec = resp.get(p.stem)
        for u in pkt["units"]:
            segs = u["segments"]
            for i, s in enumerate(segs):
                r = (rec or {}).get(s["id"])
                ko = r.get("ko") if r else None
                if r is None:
                    report["untranslated"].append(s["id"])
                    continue
                if ko is None or (isinstance(ko, str) and ko.startswith("[BROKEN")):
                    continue
                base_end = segs[i + 1]["source_byte_end"] if s["kind"] == "ruby_reading" and i + 1 < len(segs) else None
                per.setdefault(u["file"].lower(), []).append(
                    (s["source_byte_start"], s["source_byte_end"], s["kind"], ko, s["id"], base_end))
    return per


def build_file(name, a_raw, b_raw, items):
    ta, tb = a_raw.decode("cp932"), b_raw.decode("cp932")
    oa, ob = char_offsets(a_raw), char_offsets(b_raw)
    assert len(oa) == len(ta) + 1 and len(ob) == len(tb) + 1, name
    a_b2c = {b: c for c, b in enumerate(oa)}
    ops = difflib.SequenceMatcher(None, ta, tb, autojunk=False).get_opcodes() if a_raw != b_raw else [("equal", 0, len(ta), 0, len(tb))]

    def map_start(p):
        for t, i1, i2, j1, j2 in ops:
            if t == "insert" and i1 == p:
                return j2
        for t, i1, i2, j1, j2 in ops:
            if i1 <= p < i2:
                return j1 + (p - i1) if t == "equal" else j1
        return len(tb)

    def map_end(p):
        for t, i1, i2, j1, j2 in ops:
            if t == "insert" and i1 == p:
                return j1
        for t, i1, i2, j1, j2 in ops:
            if i1 < p <= i2:
                return j1 + (p - i1) if t == "equal" else j2
        return 0

    reps = []   # (b_char_start, b_char_end, new_text_str or None for bytes, new_bytes)
    for a_s, a_e, kind, ko, sid, base_end in items:
        cs, ce = a_b2c[a_s], a_b2c[a_e]
        if kind == "ruby_reading" and ko == "":
            # omitted reading: drop the ruby tag around the base
            op_s, op_e = cs - len('<r text="'), ce + len('">')
            cb = a_b2c.get(base_end)
            while cb is not None and cb < len(ta) and ta[cb] in " 　":
                cb += 1
            if ta[op_s:cs].lower() == '<r text="' and ta[ce:op_e] == '">' and cb is not None and ta[cb:cb + 4].lower() == "</r>":
                for x, y in ((op_s, op_e), (cb, cb + 4)):
                    bs, be = map_start(x), map_end(y)
                    if tb[bs:be].lower() == "</r>" or re.fullmatch(r'(?i)<r text="[^"]*">', tb[bs:be]):
                        reps.append((bs, be, b""))
                    elif a_raw != b_raw:
                        raise ValueError(f"omitted ruby {sid}: tag moved in B")
                    else:
                        report["omit_fallback"].append((sid, tb[bs:be][:40]))
            else:
                report["omit_fallback"].append((sid, ta[op_s:op_e][:40]))
            continue
        bs, be = map_start(cs), map_end(ce)
        if kind in ("ruby_reading", "choice_text"):
            if re.search(r'["<>]', tb[bs:be]) or be < bs:
                # find the attribute value nearest to the mapped spot
                attr = 'text="' if kind == "ruby_reading" else 'TEXT="'
                k = tb.rfind(attr, 0, bs + len(attr))
                if k == -1 or k < bs - 400:
                    k = tb.lower().find(attr.lower(), max(0, bs - 400))
                if k == -1:
                    report["attr_fallback"].append((sid, "attr not found"))
                    continue
                vs = k + len(attr)
                ve = tb.find('"', vs)
                report["attr_fallback"].append((sid, tb[vs:ve][:30]))
                bs, be = vs, ve
            if '"' in ko:
                ko = ko.replace('"', "”")
            reps.append((bs, be, enc(ko)))
            continue
        span = tb[bs:be]
        if TAGRE.search(span) and not TAGRE.search(ta[cs:ce]):
            # B put tags inside this A text span: keep B's tags in order, Korean goes into the first text piece and
            # the other Japanese pieces go; punctuation-only spans (da0120ed 「……<wait>……」) stay as B wrote them
            if not JPCHAR.search(re.sub(TAGRE, "", span)):
                continue
            parts = re.split(r'(<(?:[^>"]|"[^"]*")*>)', span)
            texts = [i for i in range(0, len(parts), 2) if parts[i].strip(" 　\r\n")]
            best = texts[0] if texts else 0
            rub = [pc for pc in parts[1::2] if RUBYTAG.match(pc)]
            drop_ruby = sum(1 for x in rub if not x.startswith("</")) == sum(1 for x in rub if x.startswith("</"))
            out = []
            for i, pc in enumerate(parts):
                if i % 2:
                    if not (RUBYTAG.match(pc) and drop_ruby):
                        out.append(pc.encode("cp932"))
                elif i == best:
                    out.append(enc(ko))
            report["conflicts"].append((sid, span[:60]))
            reps.append((bs, be, b"".join(out)))
            continue
        reps.append((bs, be, enc(ko)))
    reps.sort(key=lambda x: (x[0], x[1]))
    for x, y in zip(reps, reps[1:]):
        if y[0] < x[1]:
            raise ValueError(f"overlapping replacements {x[:2]} {y[:2]}")
    out, pos = [], 0
    for bs, be, nb in reps:
        out.append(b_raw[ob[pos]:ob[bs]])
        out.append(nb)
        pos = be
    out.append(b_raw[ob[pos]:])
    return b"".join(out)


def add_side_lines(raw):
    side = json.loads(SIDE.read_text(encoding="utf-8"))["lines"]
    src = {x["src_lines"]: x for x in json.loads(SIDE_IN.read_text(encoding="utf-8"))["lines"]}
    a, b = SE_FIX[0].encode("cp932"), SE_FIX[1].encode("cp932")
    n = raw.count(a)
    raw = raw.replace(a, b)
    report["side"].append(f"SE quote fixed x{n}")
    for line in side:
        jp = src[line["src_lines"]]["jp"].replace("\n", "\r").encode("cp932")
        n = raw.count(jp)
        if n == 1:
            raw = raw.replace(jp, enc(line["ko"]))
            report["side"].append(f"{line['src_lines']}: ok")
        else:
            report["side"].append(f"{line['src_lines']}: JP found {n} times — left")
    return raw


def add_rp_hooks(name, raw, g_raw):
    g = g_raw.decode("cp932")
    t = raw.decode("cp932")
    gt = [(m.start(), m.end(), m.group(0)) for m in TAGRE.finditer(g) if not RUBYTAG.match(m.group(0))]
    rp = [x for x in gt if RPRE.fullmatch(x[2])]
    if not rp:
        return raw
    non = [x for x in gt if not RPRE.fullmatch(x[2])]
    bt = [(m.start(), m.end(), m.group(0)) for m in TAGRE.finditer(t) if not RUBYTAG.match(m.group(0))]
    key = lambda s: re.sub(r"\s+", " ", s)
    g2b = {}
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, [key(x[2]) for x in non], [key(x[2]) for x in bt], autojunk=False).get_opcodes():
        if op == "equal":
            for k in range(i2 - i1):
                g2b[i1 + k] = j1 + k
    ob = char_offsets(raw)
    ins = []   # (byte pos, bytes)
    lead = []
    for s, e, tag in rp:
        nxt = next((i for i, x in enumerate(non) if x[0] >= e), None)
        if nxt == 0:
            lead.append(tag)
            continue
        bi = None
        if nxt is not None:
            for i in range(nxt, min(nxt + 5, len(non))):
                if i in g2b:
                    bi = g2b[i]
                    break
        if bi is None:
            report["rp_unplaced"].append((name, tag))
            continue
        ins.append((ob[bt[bi][0]], tag.encode("cp932")))
    for p, b in sorted(ins, reverse=True):
        raw = raw[:p] + b + raw[p:]
    if lead:
        raw = "".join(x + "\r" for x in lead).encode("cp932") + raw
    return raw


def wrap_lines(name, raw, caps):
    default = caps.get(0, 29.0)
    lines = raw.split(b"\r")
    out, cur = [], 0
    for lb in lines:
        line = lb.decode("cp932")
        mm = re.search(r'<BOX\s+type="(\d+)"', line, re.I)
        if mm:
            cur = int(mm.group(1))
        cap = caps.get(cur, default)
        if not any(c in CARRIERS for c in line):
            out.append(lb)
            continue
        head = HEAD.match(line).group(0)
        tail = TAILTAG.search(line).group(1)
        core = line[len(head):len(line) - len(tail)] if tail else line[len(head):]
        if width(head) + width(core) <= cap:
            out.append(lb)
            continue
        # spaces at the edge of a ruby base go outside it, so the line can break there (da9906ed 「<R> 뵙게</R>」)
        core2 = RUBY_ONE.sub(lambda m: (" " if m.group(4).startswith(" ") else "") + m.group(1) + m.group(2) + m.group(3)
                             + m.group(4).strip(" ") + m.group(5) + (" " if m.group(4).endswith(" ") else ""), core)
        core2 = split_wide_rubies(name, core2, cap - max(width(head), 1.0))
        if core2 != core:
            core = core2
            lb = (head + core + tail).encode("cp932")
            if width(head) + width(core) <= cap:
                out.append(lb)
                continue
        why = None
        if width(head) > 2.0 or "　　　" in core:
            why = "staged"          # placed with runs of spaces: left as the original lays it out
        else:
            parts = wrap(core, cap - width(head), cap - 1.0)
            if len(parts) < 2 or max(width(p) for p in parts) > cap:
                # no break point outside the rubies: cut every multi-word ruby at its spaces and try again
                core3 = split_wide_rubies(name, core, 0.1)
                if core3 != core:
                    core = core3
                    lb = (head + core + tail).encode("cp932")
            if " " not in re.sub(TAGRE, "", RB.sub(lambda m: m.group(2).replace(" ", ""), core)):
                why = "no_space"
        if why:
            report["wrap_skipped"][why] = report["wrap_skipped"].get(why, 0) + 1
            out.append(lb)
            continue
        parts = wrap(core, cap - width(head), cap - 1.0)
        new = [head + parts[0]] + [SP + x for x in parts[1:]]
        new[-1] += tail
        report["wrapped"] += 1
        out.extend(x.encode("cp932") for x in new)
    return b"\r".join(out)


def stage_fix(name, raw, caps):
    """11-05: a line placed with leading full-width spaces that runs past the box edge is folded by the engine
    mid-word (da0120ed 「지금, 행복하니……?」: 31 spaces put 「지 at the right edge and the rest at x=0 below). Move
    every such line of that message left by the same number of spaces (keeps their stagger) so the widest one ends a
    full-width space before the edge. Lines with gaps of spaces inside the text are left as they are."""
    if METRICS != "engine":
        return raw
    text = raw.split(b"\r")
    boxes, cur = [], 0
    for lb in text:
        m = re.search(rb'<BOX\s+type="(\d+)"', lb, re.I)
        if m:
            cur = int(m.group(1))
        boxes.append(cur)
    default = caps.get(0, 29.0)
    i = 0
    while i < len(text):
        j = i
        while j < len(text) and text[j] != b"":
            j += 1
        staged, worst = [], 0.0
        for k in range(i, j):
            line = text[k].decode("cp932")
            head = HEAD.match(line).group(0)
            core = line[len(head):]
            if width(head) <= 2.0 or not visible(core).strip() or "　　　" in core:
                continue
            staged.append(k)
            worst = max(worst, width(head) + width(core) - caps.get(boxes[k], default))
        if staged and worst > 0:
            shift = int(worst + 1.0) + (0 if (worst + 1.0).is_integer() else 1)
            for k in staged:
                line = text[k].decode("cp932")
                head = HEAD.match(line).group(0)
                text[k] = (SP * max(0, round(width(head)) - shift) + line[len(head):]).encode("cp932")
            report["stage_fix"].append((name, len(staged), shift))
        i = j + 1
    return b"\r".join(text)


# staged lines whose engine fold would land inside a ruby: break at the gap the original left with full-width spaces
LAYOUT_FIX = {
    "da1301.nps": [("　　　「어째서, 나를 버렸느냐!?　　<r text=\"・・\">아버지</r>!」",
                    "　　　「어째서, 나를 버렸느냐!?\r　　　　<r text=\"・・\">아버지</r>!」")],
    # 르뤼에 이본 주문: 원문 한자 16자(루비 = 발음되는 주문)는 원작처럼 한 줄, 괄호 해석은 새 줄에서 두 줄;
    # 상자 5(3줄)에 한 창으로 넣으려고 앞 여백 줄을 뺌
    "da9903.nps": [("<BOX type=\"5\">\r　\r<VOICE NAME=\"ルルイエ異本\" SRC=\"Da99\\0302541\">",
                    "<BOX type=\"5\">\r<VOICE NAME=\"ルルイエ異本\" SRC=\"Da99\\0302541\">"),
                   ("我招喚鬼械神鬼歹老海的騎魚的武士</r>(나\r　귀계신 크툴루의 물고기를 탄 무사를\r　소환하노라)」",
                    "我招喚鬼械神鬼歹老海的騎魚的武士</r>\r　(나 귀계신 크툴루의 물고기를 탄\r　무사를 소환하노라)」")],
    "da1303.nps": [("　" * 6 + "열린다／<wait time=\"7\">열어젖힌다　<wait time=\"7\">　<r text=\"헤븐즈 도어\">최후의 열쇠</r>　",
                    "　" * 6 + "열린다／<wait time=\"7\">열어젖힌다\r" + "　" * 6
                    + "<wait time=\"7\">　<r text=\"헤븐즈 도어\">최후의 열쇠</r>　")],
}


def layout_fix(name, raw):
    for old, new in LAYOUT_FIX.get(name, []):
        ob, nb = enc(old), enc(new)
        n = raw.count(ob)
        report["layout_fix"].append((name, n))
        if n == 1:
            raw = raw.replace(ob, nb)
    return raw


def tags_of(raw, drop_rp):
    t = raw.decode("cp932", "replace")
    return [re.sub(r'(?i)(<CHOICE\b[^>]*?\bTEXT=)"[^"]*"', r'\1""', re.sub(r"\s+", " ", m.group(0))) for m in TAGRE.finditer(t)
            if not RUBYTAG.match(m.group(0)) and not (drop_rp and RPRE.fullmatch(m.group(0)))]


def jp_left(raw):
    txt = re.sub(TAGRE, "", raw.decode("cp932", "replace"))
    return [c for c in JPCHAR.findall(txt) if c not in CARRIERS]


def ruby_balance(raw):
    t = raw.decode("cp932", "replace")
    return len(re.findall(r"(?i)<r\s", t)) - len(re.findall(r"(?i)</r>", t))


def main():
    g_pak, sys_pak, out_pak = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    rep_path = Path(sys.argv[sys.argv.index("--report") + 1]) if "--report" in sys.argv else out_pak.with_suffix(".report.json")
    _, A = load_pak(SRC_A)
    _, B = load_pak(SRC_B)
    ga, G = load_pak(g_pak)
    caps = box_caps(sys_pak)
    per = load_text()
    work = out_pak.parent / (out_pak.stem + "_files")
    if work.exists():
        shutil.rmtree(work)
    names = {e.name.rsplit("\\", 1)[-1].lower(): e.name for e in ga.entries}
    built = 0
    for name, items in sorted(per.items()):
        if name not in B:
            raise SystemExit(f"{name}: not in source B")
        # build on B (1.10) and on A; keep B unless it fails, leaves more Japanese or breaks ruby pairs (1.10
        # moved or rewrote the passage: da0203, da0701, da1503a; added lines our A-based text lacks: da0406)
        logs = ("conflicts", "attr_fallback", "omit_fallback", "side", "rp_unplaced", "wrap_skipped", "wrapped",
                "ruby_split", "ruby_unsplit", "layout_fix", "stage_fix")
        variants = {}
        for label, base in (("B", B[name]), ("A", A[name])):
            snap = {k: (report[k].copy() if isinstance(report[k], (list, dict)) else report[k]) for k in logs}
            try:
                raw = build_file(name, A[name], base, items)
                if name == "da0202.nps":
                    raw = add_side_lines(raw)
                raw = add_rp_hooks(name, raw, G[name])
                raw = stage_fix(name, raw, caps)
                raw = wrap_lines(name, raw, caps)
                raw = layout_fix(name, raw)
                variants[label] = (raw, base, {k: report[k] for k in logs}, None)
            except ValueError as ex:
                variants[label] = (None, base, None, str(ex))
            for k in logs:
                report[k] = snap[k]
        rawB, rawA = variants["B"][0], variants["A"][0]
        why = variants["B"][3]
        if why is None:
            nb, na = len(jp_left(rawB)), len(jp_left(rawA))
            if nb > na:
                why = f"B leaves {nb - na} more Japanese characters"
            elif ruby_balance(rawB) != ruby_balance(B[name]):
                why = "B breaks ruby pairs"
        label = "B" if why is None else "A"
        if why:
            report["fallback_A"].append((name, why))
        raw, base, logged, _ = variants[label]
        for k in logs:
            report[k] = logged[k]
        # checks: tags as in the base (plus the RP hooks), closed ending, no Japanese left outside tags
        tb, tn = tags_of(base, False), tags_of(raw, True)
        if name == "da0202.nps":
            tb = tags_of(base.replace(SE_FIX[0].encode("cp932"), SE_FIX[1].encode("cp932")), False)
        if ruby_balance(raw) != ruby_balance(base):
            report["tag_check"].append((name, f"ruby balance {ruby_balance(raw)} vs base {ruby_balance(base)}"))
        if tb != tn:
            sm = difflib.SequenceMatcher(None, tb, tn, autojunk=False)
            diff = [(op, tb[i1:i2][:2], tn[j1:j2][:2]) for op, i1, i2, j1, j2 in sm.get_opcodes() if op != "equal"]
            report["tag_check"].append((name, diff[:4]))
        i = raw.rfind(b"<")
        if not re.search(rb">[\r\n\x1a\x00 \t]*\Z", raw[i:]):
            report["tag_check"].append((name, "bad ending"))
        jp = jp_left(raw)
        if jp:
            report["jp_left"][name] = "".join(jp[:40])
        dst = work / Path(names[name].replace("\\", "/"))
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(raw)
        built += 1
    res = K.build_archive(g_pak, work, out_pak)
    report["built_files"] = built
    report["caps"] = caps
    rep_path.write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    print("built", built, "files ->", out_pak, "| unencodable", report["unencodable"], "| conflicts", len(report["conflicts"]),
          "| attr fallback", len(report["attr_fallback"]), "| omit fallback", len(report["omit_fallback"]),
          "| rp unplaced", len(report["rp_unplaced"]), "| untranslated", len(report["untranslated"]),
          "| wrapped", report["wrapped"], report["wrap_skipped"], "| tag check", len(report["tag_check"]),
          "| files with JP left", len(report["jp_left"]), "| built on A", [x[0] for x in report["fallback_A"]])
    print("side:", report["side"])


if __name__ == "__main__":
    main()
