"""Print the units containing the given segment ids (compact reading form).
Usage: show_ids.py da0505:T00035 da0506:T00100 ...
"""
import subprocess
import sys

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
from load import PKG, jload, packets


def main():
    want = set(sys.argv[1:])
    index = {}
    for pid in packets():
        pkt = jload(PKG / "packets" / f"{pid}.json")
        for n, u in enumerate(pkt["units"], 1):
            for s in u["segments"]:
                if s["id"] in want:
                    index.setdefault((pid, n), set()).add(s["id"])
    for (pid, n), ids in sorted(index.items()):
        print(f"=== {pid} unit#{n} wants {sorted(ids)}")
        out = subprocess.run([sys.executable, "-I", "-X", "utf8", r"P:\AI\khd-argon-review\tools\dump_packet.py",
                              pid, str(max(1, n - 1)), str(n + 1)], capture_output=True, text=True, encoding="utf-8")
        print(out.stdout)


if __name__ == "__main__":
    main()
