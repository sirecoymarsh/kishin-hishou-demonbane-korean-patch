"""Print a packet in a compact reading format: per unit the speaker, the
Japanese with tags, the current Korean per segment (with ids), and the
existing game translation.  Usage: dump_packet.py P0015_da0112 [--from N]
"""
import sys

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
from load import load_packet


def main():
    pid = sys.argv[1]
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    end = int(sys.argv[3]) if len(sys.argv) > 3 else 10**9
    resp_dir = sys.argv[4] if len(sys.argv) > 4 else "responses"
    pkt, resp, ko, ko0, rvu = load_packet(pid, resp_dir)
    for n, u in enumerate(pkt["units"], 1):
        if n < start or n > end:
            continue
        r = rvu.get(u["id"], {})
        flag = "R" if r.get("source_reviewed") else "-"
        spk = u.get("speaker_jp") or "地"
        print(f"## {u['id']} [{flag}] {spk} box={u.get('box')}")
        print("JP:", u["source_context_with_tags"].replace("\n", "⏎"))
        parts = []
        for s in u["segments"]:
            k = ko.get(s["id"])
            tag = s["id"].split(":")[1]
            if s["kind"] != "text":
                parts.append(f"[{tag} {s['kind']}:{s['jp']}→{k}]")
            else:
                parts.append(f"[{tag}]{k}")
        print("KO:", "".join(parts).replace("\n", "⏎"))
        changed = [s["id"].split(":")[1] for s in u["segments"] if ko.get(s["id"]) != ko0.get(s["id"])]
        if changed:
            print("G0:", "".join(f"[{s['id'].split(':')[1]}]{ko0.get(s['id'])}" for s in u["segments"] if s["id"].split(":")[1] in changed).replace("\n", "⏎"))
        ex = r.get("existing_game_translation")
        if ex is not None:
            print("OLD:", ex.replace("\n", "⏎"), f"({r.get('existing_alignment')})")
        print()


if __name__ == "__main__":
    main()
