"""Candidate missing spaces where two Korean text segments are displayed
back-to-back: across a mid-line command (wait/SE/k) or across ruby groups.

A join is a candidate when the left side ends in Hangul or sentence
punctuation, the right side starts with Hangul, neither side has a space,
and the right side does not start with a particle/ending (which attaches).
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
from load import PKG, packets, segments

OUT = Path(r"P:\AI\khd-argon-review\work\scan_joins.tsv")
ATTACH = re.compile(
    r"^(이라고|라고|이라는|라는|이란|란|이야|이여|이며|이자|이다|이었|였|이지|이나|이냐|이니|이랑|"
    r"에게|에서|에는|에도|으로|로|와|과|은|는|이|가|을|를|의|에|도|만|께|한테|처럼|보다|까지|부터|"
    r"마저|조차|님|들|하|한|할|함|했|해|된|되|될|됨|적|스러|답|다|요|야|여|지|고|며|면|서|게|기|"
    r"던|든|씩|째|짜리|끼리|뿐|밖|대로|만큼|쯤|씨|군|양|짱|쨩|사마|짓|거리|스럽|롭|화|성|감|식)")
SENT_END = re.compile(r"[.!?！？]$")


def raw_for(fname, cache):
    if fname not in cache:
        cand = list((PKG / "source_jp_A_cp932").glob(fname + "*"))
        cache[fname] = cand[0].read_bytes() if cand else b""
    return cache[fname]


def main():
    rows = []
    cache = {}
    for pid in packets():
        segs = list(segments(pid))
        texts = [s for s in segs if s["kind"] == "text"]
        for a, b in zip(texts, texts[1:]):
            if a["uid"] != b["uid"] or a["line"] != b["line"]:
                continue
            raw = raw_for(a["unit"]["file"], cache)
            gap = raw[a["bend"]:b["bstart"]].decode("cp932", "replace")
            if "\n" in gap:
                continue
            ka, kb = a["ko"] or "", b["ko"] or ""
            if not ka or not kb or ka[-1] in " \u3000" or kb[0] in " \u3000":
                continue
            left_ok = bool(re.search(r"[가-힣]$", ka)) or bool(SENT_END.search(ka))
            if not left_ok or not re.match(r"[가-힣]", kb):
                continue
            ruby = bool(re.search(r"<[rR] ", gap)) or "</r>" in gap.lower()
            if SENT_END.search(ka):
                kind = "sentence"
            elif ATTACH.match(kb):
                continue
            else:
                kind = "ruby" if ruby else "command"
            rows.append((kind, pid, a["sid"], b["sid"], a["reviewed"], gap[:60], ka[-25:], kb[:25]))
    with OUT.open("w", encoding="utf-8") as f:
        f.write("kind\tpid\tleft\tright\treviewed\tgap\tleft_ko\tright_ko\n")
        for r in rows:
            f.write("\t".join(str(x).replace("\t", " ").replace("\n", "⏎") for x in r) + "\n")
    import collections
    print(collections.Counter(r[0] for r in rows))
    for r in rows:
        print(r[0], r[2], r[4], "|", r[6].replace("\n", "⏎"), "‖", r[7].replace("\n", "⏎"), "|", r[5][:30])


if __name__ == "__main__":
    main()
