"""Shared loader for the KishinHisho Argon review package (read-only).

Builds one row per text segment with packet/unit/speaker context, the
Japanese source, the Gemini original, the current Codex-corrected Korean and
the existing game translation (unit-level), plus whether the unit was
already reviewed.
"""
import json
from pathlib import Path

PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")


def jload(p):
    return json.loads(Path(p).read_text(encoding="utf-8-sig"))


def packets():
    return sorted(p.stem for p in (PKG / "responses").glob("*.json"))


def coverage():
    cov = {}
    for line in (PKG / "progress" / "unit_coverage.jsonl").read_text(encoding="utf-8").splitlines():
        if line.strip():
            r = json.loads(line)
            cov[r["unit_id"]] = r
    return cov


def load_packet(pid, resp_dir="responses"):
    pkt = jload(PKG / "packets" / f"{pid}.json")
    resp = jload(PKG / resp_dir / f"{pid}.json")
    orig = jload(PKG / "responses_gemini_original" / f"{pid}.json")
    rv = jload(PKG / "review_packets" / f"{pid}.json")
    ko = {r["id"]: r.get("ko") for r in resp["records"]}
    ko0 = {r["id"]: r.get("ko") for r in orig["records"]}
    rvu = {u["unit_id"]: u for u in rv["units"]}
    return pkt, resp, ko, ko0, rvu


def segments(pid, resp_dir="responses"):
    """Yield dicts for each segment in packet order."""
    pkt, resp, ko, ko0, rvu = load_packet(pid, resp_dir)
    for u in pkt["units"]:
        r = rvu.get(u["id"], {})
        segs = u["segments"]
        for i, s in enumerate(segs):
            yield {
                "pid": pid,
                "uid": u["id"],
                "sid": s["id"],
                "kind": s["kind"],
                "line": s.get("source_line"),
                "bstart": s.get("source_byte_start"),
                "bend": s.get("source_byte_end"),
                "jp": s["jp"],
                "ko": ko.get(s["id"]),
                "ko0": ko0.get(s["id"]),
                "speaker": u.get("speaker_jp"),
                "voice": u.get("voice_id"),
                "unit": u,
                "seg_index": i,
                "reviewed": r.get("source_reviewed"),
                "existing": r.get("existing_game_translation"),
            }
