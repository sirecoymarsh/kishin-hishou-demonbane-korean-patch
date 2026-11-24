"""서재 책장 확인(11-19) — 압축을 푼 게임 폴더의 ko-library 를 Edge(헤드리스)로 열고, 게임(KHDLore)이 하듯 books.ini 의 책마다
파일이 있는지(available)를 넣어 책장을 그린 뒤, 보이는 책 수·권 번호·제목을 적는다. 다리(window.chrome.webview)는 lore_merge\\tools\\view_check.py 와 같은 흉내.
  python -X utf8 shelf_check.py <게임 폴더> <기대 권수> [그림.png]
"""
import configparser
import json
import mimetypes
import sys
import time
from pathlib import Path
from urllib.parse import unquote, urlsplit

sys.path.insert(0, "P:/AI/Codex/projects/migrated-from-c/onedrive-chatgpt/월광의 카르네바레/localization/integration/revisions/library-20261103/deps/python")
from playwright.sync_api import sync_playwright  # noqa: E402

GAME, WANT = Path(sys.argv[1]), int(sys.argv[2])
SHOT = sys.argv[3] if len(sys.argv) > 3 else None
LIB = GAME / "ko-library"
c = configparser.ConfigParser(strict=False, interpolation=None)
c.optionxform = str
c.read_string((LIB / "books.ini").read_bytes().decode("utf-16"))
n = int(c["Library"]["Count"])
catalog = []
for i in range(n):
    b = c[f"Book{i}"]
    path = LIB / b["Path"]
    catalog.append(dict(index=i, id=b.get("Id", ""), title=b["Title"], path=str(path), kind="epub", available=path.is_file(),
                        packed=True, url=f"https://books.demonbane.test/{Path(b['Path']).name}", pages=[]))


def native(m):
    return {"type": "catalog", "books": catalog} if m == "catalog" else None


def route(r):
    u = urlsplit(r.request.url)
    p = LIB / (unquote(u.path.lstrip("/")) or "index.html") if u.hostname == "library.demonbane.test" else \
        LIB / "books" / unquote(u.path.lstrip("/")) if u.hostname == "books.demonbane.test" else None
    if p is None:
        r.abort()
        return
    r.fulfill(path=p, content_type=mimetypes.guess_type(p)[0] or "application/octet-stream") if p.is_file() else r.fulfill(status=404, body="")


prof = Path(r"Y:\AI\claude-work\khd-release-1118\work_shelf") / f"profile_{int(time.time())}"
with sync_playwright() as pw:
    ctx = pw.chromium.launch_persistent_context(str(prof), executable_path="C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
                                                headless=True, viewport={"width": 1600, "height": 1200}, args=["--mute-audio", "--no-first-run"])
    ctx.route("**/*", route)
    ctx.expose_function("nativeSend", native)
    ctx.add_init_script("""(()=>{const l=[];window.chrome=window.chrome||{};window.chrome.webview={addEventListener:(t,cb)=>l.push(cb),postMessage:m=>window.nativeSend(m).then(d=>{if(d)l.forEach(cb=>cb({data:d}));})};})();""")
    page = ctx.pages[0]
    page.goto("https://library.demonbane.test/index.html")
    for _ in range(100):
        if page.evaluate("document.querySelectorAll('#books .book').length>0 || !document.getElementById('empty').hidden"):
            break
        page.wait_for_timeout(100)
    page.wait_for_timeout(1500)
    got = page.evaluate("""(()=>({count:document.getElementById('count').textContent,
      cards:[...document.querySelectorAll('#books .book')].map(b=>({volume:b.dataset.volume, book:b.dataset.book, disabled:b.disabled,
        title:b.querySelector('h2')?.textContent, art:b.querySelector('.book-cover')?.classList.contains('has-art')}))}))()""")
    if SHOT:
        page.screenshot(path=SHOT)
    ctx.close()
res = dict(game=str(GAME), books_ini=n, files_present=sum(b["available"] for b in catalog), want=WANT, shelf=got,
           ok=len(got["cards"]) == WANT and [x["volume"] for x in got["cards"]] == [f"{i + 1:02d}" for i in range(WANT)]
           and all(x["art"] and not x["disabled"] for x in got["cards"]))
print(json.dumps(res, ensure_ascii=False, indent=1))
sys.exit(0 if res["ok"] else 1)
