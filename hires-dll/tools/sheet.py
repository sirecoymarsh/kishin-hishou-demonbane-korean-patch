"""시험 스크린샷 모아 보기 — 폴더의 PNG(패턴)를 1/8 크기로 줄여 한 장에 붙인다.

  python -I -X utf8 sheet.py <폴더> <패턴=*.png> <출력 png> [열=3] [축소=8]
"""
from __future__ import annotations

import glob
import os
import sys

from PIL import Image


def main():
    d, pat, out = sys.argv[1], sys.argv[2], sys.argv[3]
    cols = int(sys.argv[4]) if len(sys.argv) > 4 else 3
    k = int(sys.argv[5]) if len(sys.argv) > 5 else 8
    fs = sorted(glob.glob(os.path.join(d, pat)))
    if not fs:
        sys.exit("no files")
    ims = [Image.open(f).convert("RGB") for f in fs]
    w, h = ims[0].size[0] // k, ims[0].size[1] // k
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (w * cols, h * rows))
    for i, im in enumerate(ims):
        sheet.paste(im.resize((w, h), Image.LANCZOS), ((i % cols) * w, (i // cols) * h))
    sheet.save(out)
    for f, im in zip(fs, ims):
        print(os.path.basename(f), im.size, "blank" if im.getextrema() in (((0, 0),) * 3,) else "")


if __name__ == "__main__":
    main()
