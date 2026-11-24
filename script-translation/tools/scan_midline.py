"""Find Korean joins between segments that share one source line but are
separated by a non-ruby inline command (e.g. <wait time="3">).

Japanese needs no space there; Korean does.  Reports each boundary with the
command between, whether the Korean has a space on either side, and whether
the unit was already reviewed.
"""
import collections
import re
import sys

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
from load import PKG, packets, segments

PUNCT_END = re.compile(r"[.!?。！？…―─」』）)\]]$")
HANGUL_START = re.compile(r"^[가-힣A-Za-z0-9「『（(]")


def raw_bytes(fname):
    return (PKG / "source_jp_A_cp932" / fname).read_bytes()


def main():
    rows = []
    stats = collections.Counter()
    for pid in packets():
        segs = list(segments(pid))
        src_cache = {}
        for a, b in zip(segs, segs[1:]):
            if a["uid"] != b["uid"] or a["kind"] != "text" or b["kind"] != "text":
                continue
            if a["line"] != b["line"]:
                continue
            fname = a["unit"]["file"]
            if fname not in src_cache:
                cand = list((PKG / "source_jp_A_cp932").glob(fname + "*"))
                src_cache[fname] = cand[0].read_bytes() if cand else None
            raw = src_cache[fname]
            gap = raw[a["bend"]:b["bstart"]].decode("cp932", "replace") if raw else "?"
            if gap.startswith("<r ") or gap.startswith("</r>") or "<r text" in gap:
                continue
            ka, kb = (a["ko"] or ""), (b["ko"] or "")
            spaced = ka.endswith((" ", "\u3000")) or kb.startswith((" ", "\u3000"))
            needs = bool(PUNCT_END.search(ka)) and bool(HANGUL_START.search(kb))
            stats[(gap.split()[0] if gap.strip() else repr(gap), spaced, needs)] += 1
            if needs and not spaced:
                rows.append((pid, a["sid"], b["sid"], gap, ka, kb, a["reviewed"]))
    for k, v in sorted(stats.items(), key=lambda x: -x[1]):
        print(v, k)
    print("missing-space boundaries:", len(rows))
    for r in rows[:40]:
        print(" | ".join(str(x) for x in r))


if __name__ == "__main__":
    main()
