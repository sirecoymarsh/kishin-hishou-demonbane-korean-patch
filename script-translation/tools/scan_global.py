"""Corpus-wide mechanical scans over the current corrected Korean.

Writes one TSV per check into work/scan_<name>.tsv and prints counts with a
few examples.  Checks:
  falsefriend  JP kanji compound rendered as a Korean homograph with another
               everyday meaning (唐突→당돌, 人影→인영, 不敵→부적 ...)
  passive      double passive 날려 버려지다 and similar translationese
  charset      characters never used by the existing (working) Korean build
  midword_nl   hard newline splitting a Korean word (가-힣 ⏎ 가-힣)
  midline_sp   missing space where a mid-line command joins two sentences
  pronoun      그/그녀/그들 in voiced dialogue
  residue      kana or CJK ideographs left in Korean output
  empty        empty / missing ko for text segments
"""
import collections
import re
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
from load import PKG, packets, segments

OUT = Path(r"P:\AI\khd-argon-review\work")

# (jp substring that must be in the segment, korean regex that is suspicious)
FALSE_FRIENDS = [
    ("唐突", r"당돌"), ("奇遇", r"기우"), ("人影", r"인영"), ("敵性", r"적성"),
    ("我慢", r"아만"), ("工夫", r"공부"), ("迷惑", r"미혹"), ("退屈", r"퇴굴"),
    ("都合", r"도합"), ("遠慮", r"원려"), ("無様", r"무양"), ("油断", r"유단"),
    ("一体", r"일체"), ("不敵", r"부적"), ("真面目", r"진면목"), ("正気", r"정기"),
    ("気色", r"기색"), ("気味", r"기미"), ("億劫", r"억겁"), ("始末", r"시말"),
    ("手配", r"수배"), ("手当", r"수당"), ("手練", r"수련"), ("用心", r"용심"),
    ("悄然", r"초연"), ("憮然", r"무연"), ("愕然", r"악연"), ("依然", r"의연"),
    ("平然", r"평연"), ("無我夢中", r"무아몽중"), ("大胆不敵", r"대담부적"),
    ("本音", r"본음"), ("一味", r"일미"), ("是非", r"시비"), ("素性", r"소성"),
    ("見物", r"견물"), ("心底", r"심저"), ("狼狽", r"낭패"), ("怪訝", r"괴아"),
    ("一応", r"일응"), ("大家", r"대가"), ("心中", r"심중"), ("適当", r"적당"),
    ("微妙", r"미묘"), ("愛想", r"애상"), ("迂闊", r"우활"), ("杜撰", r"두찬"),
    ("早速", r"조속"), ("往生", r"왕생"), ("老若男女", r"노약남녀"),
    ("素人", r"소인"), ("玄人", r"현인"), ("苦笑", r"고소"), ("一目散", r"일목산"),
    ("念話", r"염화"), ("不可視", r"불가시"), ("人面疽", r"인면"),
]
# Korean-only suspicious strings (no JP check)
KO_ONLY = [r"당돌하", r"인영", r"부적한", r"부적하게"]

PASSIVE = [r"날려\s*버려지", r"날려\s*버려졌", r"날려\s*버려진", r"되어지", r"불려지",
           r"보여지", r"잊혀지", r"쓰여지", r"짜여지", r"닫혀지", r"열려지"]

PRON = re.compile(r"(?<![가-힣])(그녀|그들|그는|그가|그를|그의|그에게|그와|그도|그만이)(?![가-힣]*\s*(정도|만큼|때|점|쪽|날|후|뒤|자리|순간|말|일|것|곳))")

KANA = re.compile(r"[\u3040-\u30ff\u31f0-\u31ff]")
CJK = re.compile(r"[\u4e00-\u9fff\u3400-\u4dbf]")


def write(name, rows, header):
    p = OUT / f"scan_{name}.tsv"
    with p.open("w", encoding="utf-8") as f:
        f.write("\t".join(header) + "\n")
        for r in rows:
            f.write("\t".join(str(x).replace("\t", " ").replace("\n", "⏎") for x in r) + "\n")
    return p


def main():
    allsegs = []
    for pid in packets():
        allsegs.extend(segments(pid))
    print("segments:", len(allsegs), "text:", sum(1 for s in allsegs if s["kind"] == "text"))

    # charset of existing build
    old_chars = collections.Counter()
    for s in allsegs:
        if s["seg_index"] == 0 and s["existing"]:
            old_chars.update(s["existing"])
    new_chars = collections.Counter()
    for s in allsegs:
        if s["ko"]:
            new_chars.update(s["ko"])
    unseen = {c: n for c, n in new_chars.items() if c not in old_chars}
    rows = []
    for s in allsegs:
        k = s["ko"] or ""
        bad = sorted({c for c in k if c in unseen})
        if bad:
            rows.append((s["pid"], s["sid"], s["reviewed"], "".join(bad), s["jp"], k))
    write("charset", rows, ["pid", "sid", "reviewed", "chars", "jp", "ko"])
    print(f"\n[charset] chars not in existing build: {len(unseen)} kinds, {len(rows)} segments")
    print("  ", " ".join(f"{c}({n})" for c, n in sorted(unseen.items(), key=lambda x: -x[1])[:60]))

    # false friends
    rows = []
    for s in allsegs:
        k = s["ko"] or ""
        for jp, pat in FALSE_FRIENDS:
            if jp in s["jp"] and re.search(pat, k):
                rows.append((jp, pat, s["pid"], s["sid"], s["reviewed"], s["speaker"], s["jp"], k))
        for pat in KO_ONLY:
            if re.search(pat, k) and not any(jp in s["jp"] for jp, p in FALSE_FRIENDS if p in pat):
                rows.append(("(ko)", pat, s["pid"], s["sid"], s["reviewed"], s["speaker"], s["jp"], k))
    write("falsefriend", rows, ["jp", "pat", "pid", "sid", "reviewed", "speaker", "jp_text", "ko"])
    c = collections.Counter((r[0], r[1]) for r in rows)
    print(f"\n[falsefriend] {len(rows)} hits:", ", ".join(f"{a}→{b}:{n}" for (a, b), n in c.most_common()))

    # passive
    rows = []
    for s in allsegs:
        k = s["ko"] or ""
        for pat in PASSIVE:
            if re.search(pat, k):
                rows.append((pat, s["pid"], s["sid"], s["reviewed"], s["jp"], k))
    write("passive", rows, ["pat", "pid", "sid", "reviewed", "jp", "ko"])
    c = collections.Counter(r[0] for r in rows)
    print(f"\n[passive] {len(rows)} hits:", ", ".join(f"{a}:{n}" for a, n in c.most_common()))

    # mid-word newline
    rows = []
    for s in allsegs:
        k = s["ko"] or ""
        for m in re.finditer(r"([가-힣])\n([\u3000 ]*)([가-힣])", k):
            if m.group(2) == "":
                rows.append((s["pid"], s["sid"], s["reviewed"], k[max(0, m.start() - 8):m.end() + 6], s["jp"]))
    write("midword_nl", rows, ["pid", "sid", "reviewed", "around", "jp"])
    print(f"\n[midword_nl] {len(rows)} hard newlines directly between two Hangul syllables (no indent)")

    # newline structure: total hard newlines without indent in ko vs jp
    nl_ko = sum((s["ko"] or "").count("\n") for s in allsegs)
    nl_jp = sum(s["jp"].count("\n") for s in allsegs)
    print(f"   hard newlines inside segments: jp={nl_jp} ko={nl_ko}")

    # pronoun in dialogue
    rows = []
    for s in allsegs:
        k = s["ko"] or ""
        if s["speaker"] and PRON.search(k):
            rows.append((s["pid"], s["sid"], s["reviewed"], s["speaker"], s["jp"], k))
    write("pronoun", rows, ["pid", "sid", "reviewed", "speaker", "jp", "ko"])
    print(f"\n[pronoun] voiced segments with 그/그녀/그들: {len(rows)}")

    # residue
    rows = []
    for s in allsegs:
        k = s["ko"] or ""
        if KANA.search(k) or CJK.search(k):
            rows.append((s["pid"], s["sid"], s["reviewed"], s["kind"], s["jp"], k))
    write("residue", rows, ["pid", "sid", "reviewed", "kind", "jp", "ko"])
    print(f"\n[residue] segments with kana/kanji in ko: {len(rows)}")
    for r in rows[:12]:
        print("   ", r)

    # empty
    rows = [(s["pid"], s["sid"], s["kind"], s["jp"], repr(s["ko"])) for s in allsegs
            if s["kind"] == "text" and not (s["ko"] or "").strip() and s["jp"].strip()]
    write("empty", rows, ["pid", "sid", "kind", "jp", "ko"])
    print(f"\n[empty] text segments with empty ko but non-empty jp: {len(rows)}")
    for r in rows[:12]:
        print("   ", r)


if __name__ == "__main__":
    main()
