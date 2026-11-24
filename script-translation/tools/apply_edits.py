"""Apply a reviewed edit list to a copy of the package responses.

Reads responses from <src> (default: the pristine package copy), writes all
104 response files to <dst>, applies edits with exact-match assertions,
validates every changed packet with the package's own validator, and writes
a review log with real authorship.  Never touches the Codex delivery folder.

Usage: apply_edits.py edits.json [edits2.json ...]
"""
import copy
import datetime
import hashlib
import importlib.util
import json
import sys
from pathlib import Path

PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")
OUT = Path(r"P:\AI\khd-argon-review\out")
DST = OUT / "responses"
LOGS = OUT / "review_logs"
MODEL = "Claude Opus 5.5 (claude-opus-5-5)"


def jload(p):
    return json.loads(Path(p).read_text(encoding="utf-8-sig"))


def main():
    global DST, LOGS
    args = sys.argv[1:]
    if args and args[0].startswith("--dst="):
        DST = Path(args[0][6:]) / "responses"
        LOGS = Path(args[0][6:]) / "review_logs"
        args = args[1:]
    edits = []
    for p in args:
        for e in jload(p):
            e["_file"] = Path(p).name
            edits.append(e)
    DST.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)
    resp = {p.stem: jload(p) for p in sorted((PKG / "responses").glob("*.json"))}
    before = copy.deepcopy(resp)
    where = {}
    for pid, r in resp.items():
        for i, rec in enumerate(r["records"]):
            where[rec["id"]] = (pid, i)
    log = []
    errors = []
    for e in edits:
        if e.get("op") == "replace_all_chars":
            for pid, r in resp.items():
                for rec in r["records"]:
                    ko = rec.get("ko")
                    if isinstance(ko, str) and e["old"] in ko:
                        new = ko.replace(e["old"], e["new"])
                        log.append({"id": rec["id"], "packet": pid, "before": ko, "after": new,
                                    "category": e["cat"], "reason": e["reason"]})
                        rec["ko"] = new
            continue
        sid = e["sid"]
        if sid not in where:
            errors.append(f"{sid}: no such record")
            continue
        pid, i = where[sid]
        rec = resp[pid]["records"][i]
        ko = rec.get("ko")
        if not isinstance(ko, str):
            errors.append(f"{sid}: ko is not a string")
            continue
        if e.get("op") == "omit_ruby":
            # package contract: empty value + ruby_action omit_redundant + a ruby_decision issue
            if not sid.split(":")[1].startswith("R"):
                errors.append(f"{sid}: omit_ruby on a non-ruby record")
                continue
            rec["ko"] = ""
            rec["ruby_action"] = "omit_redundant"
            issues = resp[pid].setdefault("issues", [])
            if not any(q.get("id") == sid for q in issues):
                issues.append({"id": sid, "kind": "ruby_decision", "note": e["reason"]})
            log.append({"id": sid, "packet": pid, "before": ko, "after": "", "category": e["cat"],
                        "reason": e["reason"] + " (ruby_action=omit_redundant)"})
            continue
        if e.get("op") == "prefix":
            if ko.startswith(e["new"]):
                errors.append(f"{sid}: already has prefix")
                continue
            new = e["new"] + ko
        else:
            n = ko.count(e["old"])
            if n != 1:
                errors.append(f"{sid}: old text found {n} times: {e['old']!r} in {ko!r}")
                continue
            new = ko.replace(e["old"], e["new"])
        log.append({"id": sid, "packet": pid, "before": ko, "after": new,
                    "category": e["cat"], "reason": e["reason"]})
        rec["ko"] = new
    if errors:
        print("EDIT ERRORS:")
        for x in errors:
            print("  ", x)
        raise SystemExit(1)

    spec = importlib.util.spec_from_file_location("vc", PKG / "tools" / "validate_corrected.py")
    vc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(vc)
    changed = sorted({x["packet"] for x in log})
    results = {}
    for pid, r in resp.items():
        (DST / f"{pid}.json").write_text(json.dumps(r, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    for pid in changed:
        res = vc.validate(jload(PKG / "packets" / f"{pid}.json"), resp[pid])
        results[pid] = {"status": res["status"], "errors": res["errors"]}
    stamp = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    (LOGS / "claude_edits.jsonl").write_text(
        "\n".join(json.dumps({**x, "author_model": MODEL, "at": stamp}, ensure_ascii=False) for x in log) + "\n",
        encoding="utf-8")
    (LOGS / "validation.json").write_text(json.dumps(results, ensure_ascii=False, indent=1), encoding="utf-8")
    fails = {k: v for k, v in results.items() if v["errors"]}
    print(f"edits applied: {len(log)} fields in {len(changed)} packets; validation fails: {len(fails)}")
    for k, v in fails.items():
        print("  FAIL", k, v["errors"][:5])
    # unchanged packets must be byte-identical in value
    same = sum(1 for pid in resp if pid not in changed and resp[pid] == before[pid])
    print(f"untouched packets identical: {same}/{len(resp) - len(changed)}")


if __name__ == "__main__":
    main()
