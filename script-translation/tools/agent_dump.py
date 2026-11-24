"""Reading view for review agents: one packet, every unit, with the current
Korean from out/responses (round-1 edits included).

  python -I -X utf8 agent_dump.py P0016_da0113 [from_unit] [to_unit]

Per unit:
  ## <unit id> [검수 대상|검수됨] <speaker or 지문> voice=<id> box=<n>
  JP:  source with engine tags, ⏎ = newline
  KO:  [T00012]본문 칸 ... [R00003 읽기:<jp>→<ko>] ruby reading cells
  OLD: older independent Korean translation (reference only; may be shifted
       or truncated - never copy it as the answer)
"""
import json
import sys
from pathlib import Path

PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")
CUR = Path(r"P:\AI\khd-argon-review\out\responses")


def jload(p):
    return json.loads(Path(p).read_text(encoding="utf-8-sig"))


def main():
    pid = sys.argv[1]
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    end = int(sys.argv[3]) if len(sys.argv) > 3 else 10**9
    pkt = jload(PKG / "packets" / f"{pid}.json")
    ko = {r["id"]: r.get("ko") for r in jload(CUR / f"{pid}.json")["records"]}
    rvu = {u["unit_id"]: u for u in jload(PKG / "review_packets" / f"{pid}.json")["units"]}
    total = len(pkt["units"])
    todo = sum(1 for u in pkt["units"] if not rvu.get(u["id"], {}).get("source_reviewed"))
    print(f"# {pid}  file={pkt['file']}  units={total}  검수 대상={todo}")
    for n, u in enumerate(pkt["units"], 1):
        if n < start or n > end:
            continue
        r = rvu.get(u["id"], {})
        flag = "검수됨" if r.get("source_reviewed") else "검수 대상"
        spk = u.get("speaker_jp") or "지문"
        print(f"## {u['id']} [{flag}] {spk} voice={u.get('voice_id')} box={u.get('box')}")
        print("JP:", u["source_context_with_tags"].replace("\n", "⏎"))
        parts = []
        for s in u["segments"]:
            k = ko.get(s["id"])
            tag = s["id"].split(":")[1]
            if s["kind"] != "text":
                parts.append(f"[{tag} 읽기:{s['jp']}→{k if k else ''}]")
            else:
                parts.append(f"[{tag}]{k if k is not None else '(원문 바이트 보존)'}")
        print("KO:", "".join(parts).replace("\n", "⏎"))
        ex = r.get("existing_game_translation")
        if ex:
            print("OLD:", ex.replace("\n", "⏎"))
        print()


if __name__ == "__main__":
    main()
