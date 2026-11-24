"""Turn agent proposal files + Claude decisions into an apply_edits.py list.

Only agents listed in work/decisions.json are included (an agent is added
there once its proposals have been read).  Writes work/edits_round2.json.
"""
import json
from pathlib import Path

ROOT = Path(r"P:\AI\khd-argon-review")
AG = ROOT / "work" / "agents"


def main():
    dec = json.loads((ROOT / "work" / "decisions.json").read_text(encoding="utf-8"))["accept_all_except"]
    out, stats = [], {}
    for agent, exceptions in sorted(dec.items()):
        data = json.loads((AG / f"{agent}.json").read_text(encoding="utf-8-sig"))
        acc = rej = mod = 0
        for i, e in enumerate(data["edits"]):
            key = f"{agent}:{i}"
            d = exceptions.get(key)
            if d and d["action"] == "reject":
                rej += 1
                continue
            item = {k: e[k] for k in ("sid", "old", "new", "op") if k in e}
            item["cat"] = e.get("cat", "기타")
            item["reason"] = f"[{key} {e.get('grade', '')}] {e.get('reason', '')}"
            if d and d["action"] == "modify":
                item["new"] = d["new"]
                if "old" in d:
                    item["old"] = d["old"]
                item["reason"] += f" / Claude 수정: {d['why']}"
                mod += 1
            else:
                acc += 1
            out.append(item)
        stats[agent] = {"accepted": acc, "modified": mod, "rejected": rej}
    (ROOT / "work" / "edits_round2.json").write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")
    print(json.dumps(stats, ensure_ascii=False))
    print("edits_round2:", len(out))


if __name__ == "__main__":
    main()
