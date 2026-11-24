"""전투 마법진·고리 텍스처(DDS) ↔ 2D 이펙트 그림(cg\ef) 특징점 대조(SIFT + RANSAC) — 원형 찾기"""
import sys, io, json, re
from pathlib import Path
import numpy as np, cv2
from PIL import Image
TOOLS = Path(r"P:\AI\khd-runtime-fix\tools")
src = (TOOLS / "gupk_sounds.py").read_text(encoding="utf-8").split("\nargs = sys.argv[1:]")[0]
gs = {}
_a, sys.argv = sys.argv, [sys.argv[0], r"F:\AI\khd-latest-game\demonbane.dll"]
exec(compile(src, "gupk_sounds.py", "exec"), gs)
G = Path(r"F:\AI\khd-latest-game"); TABLE = gs["read_pe_va"](str(G / "demonbane.dll"), gs["LOOKUP_TABLE_VA"], 6 * 256)
rows = json.load(open("dds_survey.json", encoding="utf-8"))
pat = re.compile(r"zin|jin|maho|ring|hamon|kekkai|ketu|baind|sigil|seal|M_LINE|mz_|048M1|NL1|kyoukai|GARD|onryou", re.I)
cands = [r for r in rows if pat.search(r["name"]) and r["w"] >= 64]
eff = {}
for pack in ("dmpack.dat", "dmpack001.dat", "dmpack002.dat", "dmpack003.dat"):
    for idx, name, key, unpacked, off, size in gs["entries"](str(G / pack), TABLE):
        eff[name.lower()] = (pack, key, unpacked, off, size)
sift = cv2.SIFT_create(nfeatures=4000)
def gray_on_black(im):
    im = im.convert("RGBA"); bg = Image.new("RGBA", im.size, (0, 0, 0, 255)); bg.alpha_composite(im)
    return np.array(bg.convert("L"))
refs = []
for p in sorted(Path(r"Y:\AI\claude-work\khd-fx-survey\cg_ef").glob("*.png")):
    if p.name.startswith("_"): continue
    g = np.array(Image.open(p).convert("L"))
    kp, de = sift.detectAndCompute(g, None)
    if de is not None and len(kp) > 20: refs.append((p.name, kp, de))
bf = cv2.BFMatcher()
out = []
for r in cands:
    pack, key, unpacked, off, size = eff[r["name"].lower()]
    d = gs["extract"](str(G / pack), TABLE, key, unpacked, off, size)
    try:
        im = Image.open(io.BytesIO(d)); im.load()
    except Exception:
        continue
    g = gray_on_black(im.resize((im.width * 4, im.height * 4), Image.LANCZOS))
    kp, de = sift.detectAndCompute(g, None)
    if de is None or len(kp) < 10: continue
    best = []
    for nm, kp2, de2 in refs:
        ms = bf.knnMatch(de, de2, k=2)
        good = [m for m, n in (x for x in ms if len(x) == 2) if m.distance < 0.75 * n.distance]
        if len(good) < 8: continue
        a = np.float32([kp[m.queryIdx].pt for m in good]); b = np.float32([kp2[m.trainIdx].pt for m in good])
        H, mask = cv2.estimateAffinePartial2D(a, b, method=cv2.RANSAC, ransacReprojThreshold=6)
        inl = int(mask.sum()) if mask is not None else 0
        scale = float(np.sqrt(abs(np.linalg.det(H[:, :2])))) * 4 if H is not None else 0   # 2D 그림 화소 / 텍스처 화소
        best.append((inl, nm, round(scale, 2)))
    best.sort(reverse=True)
    out.append(dict(name=r["name"], size=f"{r['w']}x{r['h']}", top=best[:3]))
    if best and best[0][0] >= 15:
        print(f"{r['w']:4}x{r['h']:<4} {r['name']:44} -> {best[0][1]} (inliers {best[0][0]}, x{best[0][2]})  2nd {best[1][:2] if len(best)>1 else ''}")
json.dump(out, open("fx_match.json", "w", encoding="utf-8"), ensure_ascii=False, indent=0)
print(len(cands), "textures checked against", len(refs), "2D effect images")
