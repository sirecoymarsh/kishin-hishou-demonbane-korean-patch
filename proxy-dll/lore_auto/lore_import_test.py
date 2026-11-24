"""11-19 서고 자동 목록·자동 표지 게임 안 시험 — 숨김(화면 밖·소리 끔). 압축으로 푼 사본(기본 Y:\\AI\\claude-work\\khd-release-1118\\zipcheck\\A)에
새 KHDLore.dll·library.js·시험 책(「시험 책 1.epub」, 표지·제목은 EPUB 안에만)을 넣고, 감상실 → LORE 를 열어 WebView2 원격 디버깅(9341)으로
책장을 읽는다. 11-09 lore_scen_run.py 의 붙는 법(KHD_LORE_TEST·WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS)을 빌림. 끝나면 ini 를 되돌린다.
  python -I -X utf8 lore_auto_test.py [게임 폴더] [기대 권수]
"""
import ctypes as C
import ctypes.wintypes as W
import json
import os
import shutil
import subprocess
import sys
import time
import winreg
from pathlib import Path

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
GAME = Path(ARGS[0]) if ARGS else Path(r"Y:\AI\claude-work\khd-release-1118\zipcheck\A")
WANT = int(ARGS[1]) if len(ARGS) > 1 else 3
HERE = Path(__file__).resolve().parent
OUT = HERE / "runs" / time.strftime("%m%d_%H%M%S")
OUT.mkdir(parents=True)
(OUT / "tmp").mkdir()
sys.path.insert(0, "P:/AI/Codex/projects/migrated-from-c/onedrive-chatgpt/월광의 카르네바레/localization/integration/revisions/library-20261103/deps/python")
sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import run_smoke as S  # noqa: E402
import run_uhd_a as UA  # noqa: E402
from playwright.sync_api import sync_playwright  # noqa: E402

S.GAME = GAME
S.INI = GAME / "autobattle" / "autobattle.ini"
u = C.WinDLL("user32", use_last_error=True)
k = C.WinDLL("kernel32", use_last_error=True)
k.OpenProcess.restype = W.HANDLE
u.GetPropW.argtypes = [W.HWND, W.LPCWSTR]
u.GetPropW.restype = W.HANDLE
ENUM = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
LOG = []
EXE = str(GAME / "KishinHisho_chs.exe").lower()


def log(s):
    line = time.strftime("%H:%M:%S ") + s
    LOG.append(line)
    print(line, flush=True)


def exe_of(pid):
    h = k.OpenProcess(0x1000, False, pid)
    if not h:
        return ""
    try:
        b = C.create_unicode_buffer(1024)
        n = W.DWORD(1024)
        return b.value.lower() if k.QueryFullProcessImageNameW(h, 0, b, C.byref(n)) else ""
    finally:
        k.CloseHandle(h)


def windows(cls=None, pid=None):
    out = []

    def cb(h, _l):
        p = W.DWORD()
        u.GetWindowThreadProcessId(h, C.byref(p))
        b = C.create_unicode_buffer(64)
        u.GetClassNameW(h, b, 64)
        if (cls is None or b.value == cls) and (pid is None or p.value == pid):
            out.append((h, p.value, b.value))
        return True
    u.EnumWindows(ENUM(cb), 0)
    return out


def shot(h, name):
    """PrintWindow(창 내용) → OUT\\<name>.png, 그리고 엔진 메뉴 상태 값([system.dll+0xEE680]+0x18E2C)"""
    try:
        from PIL import Image
        r = W.RECT()
        u.GetClientRect(h, C.byref(r))
        w, hh = r.right, r.bottom
        g = C.WinDLL("gdi32")
        hdc = u.GetDC(h)
        mdc = g.CreateCompatibleDC(hdc)
        bmp = g.CreateCompatibleBitmap(hdc, w, hh)
        g.SelectObject(mdc, bmp)
        u.PrintWindow(h, mdc, 1 | 2)
        buf = C.create_string_buffer(w * hh * 4)
        bi = (C.c_uint32 * 10)(40, w, (-hh) & 0xFFFFFFFF, 1 | (32 << 16), 0, 0, 0, 0, 0, 0)
        g.GetDIBits(mdc, bmp, 0, hh, buf, bi, 0)
        Image.frombuffer("RGBA", (w, hh), buf, "raw", "BGRA", 0, 1).convert("RGB").save(OUT / f"{name}.png")
        g.DeleteObject(bmp)
        g.DeleteDC(mdc)
        u.ReleaseDC(h, hdc)
    except Exception as e:
        log(f"shot {name} failed: {e}")


def poll(page, expr, secs=30):
    """페이지 함수 기다리기(Playwright 의 것)는 페이지 안에서 eval 로 되풀이해 서고 페이지의 CSP(script-src 'self')에 막힌다 → evaluate 를 되풀이"""
    end = time.time() + secs
    while time.time() < end:
        try:
            if page.evaluate(expr):
                return True
        except Exception:
            pass
        page.wait_for_timeout(300)
    raise AssertionError(f"timed out: {expr[:80]}")


def wait(fn, secs=30, step=0.25):
    end = time.time() + secs
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


import psutil  # noqa: E402

if any((p.info["name"] or "").lower() == "kishinhisho_chs.exe" for p in psutil.process_iter(["name"])):
    sys.exit("기신비상이 켜져 있다 — 시험하지 않는다")

# 넣을 것
NEW = {"KHDLore.dll": HERE / "build" / "KHDLore.dll", "ko-library/library.js": Path(r"F:\AI\khd-latest-game\ko-library\library.js"),
       "ko-library/index.html": Path(r"F:\AI\khd-latest-game\ko-library\index.html")}
for rel, src in NEW.items():
    shutil.copy2(src, GAME / rel)
for old_test in (GAME / "ko-library" / "books").glob("시험 책*.epub"):     # 들여오기로 다시 들어오게
    old_test.unlink()
TESTBOOKS = HERE / "testbook"
log(f"put in {GAME}: " + ", ".join(NEW))
raw = S.INI.read_bytes()
enc = "utf-16" if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8"
S.set_ini(raw, enc, agent=0, advance=0, skip=0, mute=1, subtitles=0, background=1, uhd=1, uhd_mode="window", uhd_scale=1, uhd_offscreen=1)
own = r"Software\nitroplus\KishinHisho_Win11Copy"
with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, own, 0, winreg.KEY_WRITE | winreg.KEY_WOW64_32KEY) as key:
    winreg.SetValueEx(key, "FullScreen", 0, winreg.REG_DWORD, 0)      # 앱 안 덧칠 층(이 실행기의 자식 게임이 보는 값)
env = os.environ.copy()
env.update(KHD_LORE_TEST="1", KHD_LORE_PROFILE=str(OUT / "profile"), TEMP=str(OUT / "tmp"), TMP=str(OUT / "tmp"),
           WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS="--remote-debugging-port=9341 --remote-debugging-address=127.0.0.1 --disable-background-networking")
nlog0 = (GAME / "ko-library" / "native.log").stat().st_size if (GAME / "ko-library" / "native.log").exists() else 0
si = subprocess.STARTUPINFO()
si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
si.wShowWindow = 4
proc = subprocess.Popen([str(GAME / "KishinHisho_chs.exe")], cwd=GAME, env=env, startupinfo=si, creationflags=0x08000000)
res = {"game": str(GAME), "want": WANT}
pid = None
import threading  # noqa: E402
stop = threading.Event()
moved = []


def mover():
    """11-16 「안보이게」: 이 게임의 보이는 창(대화 상자 포함)을 화면 밖으로"""
    while not stop.is_set():
        if pid:
            for hh, _p, cls in windows(None, pid):
                if u.IsWindowVisible(hh):
                    rc = W.RECT()
                    u.GetWindowRect(hh, C.byref(rc))
                    if -30000 < rc.left < 15000:
                        u.SetWindowPos(hh, 1, 20900, 0, 0, 0, 0x0001 | 0x0010)
                        moved.append((cls, rc.left, rc.top))
        time.sleep(0.02)


threading.Thread(target=mover, daemon=True).start()
try:
    def game_window():
        for h, p, _c in windows("OCGameApplicationWindow"):
            if exe_of(p) == EXE:
                return h, p
        return None
    got = wait(game_window, 60)
    assert got, "no game window"
    h, pid = got
    log(f"game window {h:#x} pid {pid}")
    wait(lambda: u.GetPropW(h, "KHD.Lore.Attached"), 30)
    time.sleep(10)
    UA.layout(h)
    shot(h, "00_start")
    UA.click(h, 400, 480)
    # 새 설치 첫 실행: 「주의 … ENTER를 눌러 환경 설정 메뉴로」 안내 화면 → 누르면 환경설정 창 → 「닫기」로 닫아야 타이틀(그 뒤엔 안 나옴)
    dlg, end, last = None, time.time() + 25, 0
    while time.time() < end and not dlg:
        dlg = next((dh for dh, _p, _c in windows("#32770", pid) if u.IsWindowVisible(dh)), None)
        if not dlg and time.time() - last > 3:
            last = time.time()
            UA.click(h, 400, 300)
        time.sleep(0.3)
    if dlg:
        time.sleep(1)
        btn = []

        def findclose(c, _l):
            b = C.create_unicode_buffer(64)
            u.GetWindowTextW(c, b, 64)
            if "닫기" in b.value:
                btn.append(c)
            return True
        u.EnumChildWindows(dlg, ENUM(findclose), 0)
        if btn:
            u.PostMessageW(btn[0], 0x00F5, 0, 0)          # BM_CLICK 「닫기」
        else:
            u.PostMessageW(dlg, 0x0111, 2, 0)
        gone = wait(lambda: not u.IsWindowVisible(dlg), 10)
        log(f"first-run settings dialog closed ({'닫기' if btn else 'IDCANCEL'}): {bool(gone)}")
        time.sleep(4)
    else:
        log("no settings dialog after PRESS START")
    shot(h, "01_title")
    UA.click(h, 405, 416)                                          # 감상실
    time.sleep(4)
    shot(h, "02_extra")
    UA.click(h, 400, 474)                                          # LORE
    ok = wait(lambda: u.GetPropW(h, "KHD.Lore.Active"), 25)
    shot(h, "03_after_lore")
    log(f"LORE open: {bool(ok)}")
    assert ok, "LORE did not open"
    with sync_playwright() as pw:
        browser = None
        end = time.time() + 30
        while time.time() < end and browser is None:
            try:
                browser = pw.chromium.connect_over_cdp("http://127.0.0.1:9341", timeout=1000)
            except Exception:
                time.sleep(0.3)
        assert browser, "CDP not reachable"
        page, end = None, time.time() + 30
        while page is None and time.time() < end:       # 모든 컨텍스트의 페이지를 되풀이해 본다(Playwright 이벤트 고리를 돌리며)
            for c2 in browser.contexts:
                for pg in c2.pages:
                    if pg.url.startswith("https://library.demonbane.test/"):
                        page = pg
            if page is None and browser.contexts:
                try:
                    browser.contexts[0].wait_for_event("page", timeout=1000)
                except Exception:
                    pass
        assert page, "library page not found over CDP"
        log(f"library page: {page.url}")
        poll(page, "catalog.length>0", 20)
        poll(page, f"document.querySelectorAll('#books .book').length>={WANT} && [...document.querySelectorAll('#books .book .book-cover')].every(c=>c.classList.contains('has-art'))", 30)
        page.wait_for_timeout(1500)
        res["catalog"] = page.evaluate("catalog.map(b=>({index:b.index,id:b.id,title:b.title,available:b.available,url:b.url?b.url.replace(/^https:\\/\\/[^/]+/,'…'):''}))")
        res["shelf"] = page.evaluate("""[...document.querySelectorAll('#books .book')].map(b=>({volume:b.dataset.volume,title:b.querySelector('h2')?.textContent,
            art:b.querySelector('.book-cover').classList.contains('has-art'),cover:(b.querySelector('.cover-art')?.src||'').slice(0,40)}))""")
        res["count"] = page.evaluate("document.getElementById('count').textContent")
        page.screenshot(path=str(OUT / "shelf.png"))
        res["import_button"] = {"visible": page.locator("#add-epub").is_visible(), "text": page.locator("#add-epub").inner_text(),
                                "folder_button_visible": page.locator("#add-folder").is_visible()}
        page.screenshot(path=str(OUT / "shelf_with_button.png"))
        for round_no in (1, 2):
            page.locator("#add-epub").click()
            dlg = wait(lambda: next((dh for dh, _p, _c in windows("#32770", pid) if u.IsWindowVisible(dh)), None), 15)
            assert dlg, "no file dialog"
            tb = C.create_unicode_buffer(128)
            u.GetWindowTextW(dlg, tb, 128)
            res[f"import{round_no}_dialog"] = tb.value
            u.FindWindowExW.restype = W.HWND
            u.FindWindowExW.argtypes = [W.HWND, W.HWND, W.LPCWSTR, W.LPCWSTR]
            u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPCWSTR]
            def edit_box():
                cbx = u.GetDlgItem(dlg, 0x47C)
                cb = u.FindWindowExW(cbx, None, "ComboBox", None) if cbx else None
                return u.FindWindowExW(cb, None, "Edit", None) if cb else None
            e = wait(edit_box, 10)
            assert e, "no file name box"
            u.SendMessageW(e, 0x000C, 0, str(TESTBOOKS))                       # 폴더로 가기
            u.PostMessageW(u.GetDlgItem(dlg, 1), 0x00F5, 0, 0)
            time.sleep(2)
            e = wait(edit_box, 10)
            u.SendMessageW(e, 0x000C, 0, '"시험 책 1.epub" "시험 책 2.epub"')
            u.PostMessageW(u.GetDlgItem(dlg, 1), 0x00F5, 0, 0)
            gone = wait(lambda: not u.IsWindow(dlg) or not u.IsWindowVisible(dlg), 15)
            res[f"import{round_no}_dialog_closed"] = bool(gone)
            page.wait_for_timeout(2500)
            poll(page, f"document.querySelectorAll('#books .book').length>={WANT + 2} && [...document.querySelectorAll('#books .book .book-cover')].every(c=>c.classList.contains('has-art'))", 40)
            page.wait_for_timeout(1500)
            res[f"import{round_no}_shelf"] = page.evaluate("[...document.querySelectorAll('#books .book')].map(b=>b.dataset.volume+' '+b.querySelector('h2').textContent+(b.querySelector('.book-cover').classList.contains('has-art')?' [표지]':''))")
            res[f"import{round_no}_notice"] = page.locator("#notice").inner_text() if page.locator("#notice").is_visible() else ""
            res[f"import{round_no}_files"] = sorted(p.name for p in (GAME / "ko-library" / "books").glob("시험 책*.epub"))
            page.screenshot(path=str(OUT / f"import{round_no}.png"))
        # 시험 책을 열어 본문이 뜨는지
        idx = next((x["index"] for x in res["catalog"] if x["id"].startswith("file-")), None)
        if idx is not None:
            page.locator(f'[data-book="{idx}"]').click()
            page.wait_for_timeout(1500)
            page.locator("#loading").wait_for(state="hidden", timeout=30000)
            page.wait_for_timeout(1000)
            res["opened_title"] = page.locator("#title").inner_text()
            page.screenshot(path=str(OUT / "reading.png"))
            page.locator("#shelf-back").click()
            page.wait_for_timeout(800)
        page.locator("#exit").click()
    wait(lambda: not u.GetPropW(h, "KHD.Lore.Active"), 15)
    log("LORE closed")
finally:
    if pid:
        for _ in range(3):
            if not psutil.pid_exists(pid):
                break
            for hh, _p, _c in windows("OCGameApplicationWindow", pid):
                u.PostMessageW(hh, 0x0010, 0, 0)
            time.sleep(1.5)
            for dh, _p, _c in windows("#32770", pid):
                kids = []

                def ch(c, _l):
                    b = C.create_unicode_buffer(64)
                    u.GetClassNameW(c, b, 64)
                    if b.value == "Button":
                        kids.append(c)
                    return True
                u.EnumChildWindows(dh, ENUM(ch), 0)
                if kids:
                    u.PostMessageW(kids[0], 0x00F5, 0, 0)
            wait(lambda: not psutil.pid_exists(pid), 10)
        res["exited"] = not psutil.pid_exists(pid)
    stop.set()
    res["moved_windows"] = moved[:10]
    S.INI.write_bytes(raw)
    log("autobattle.ini restored")
    nl = GAME / "ko-library" / "native.log"
    if nl.exists():
        res["native_log"] = [ln for ln in nl.read_bytes()[nlog0:].decode("utf-8", "replace").splitlines() if "shelf" in ln or "open book" in ln or "error" in ln.lower()][:12]
    shelf = res.get("shelf") or []
    res["ok"] = (len(shelf) == WANT and all(x["art"] for x in shelf) and res.get("import_button", {}).get("visible")
                 and len(res.get("import1_shelf", [])) == WANT + 2 and len(res.get("import2_shelf", [])) == WANT + 2
                 and res.get("import2_files") == ["시험 책 1.epub", "시험 책 2.epub"] and bool(res.get("exited")))
    res["log"] = LOG
    (OUT / "result.json").write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding="utf-8")
    print(json.dumps(res, ensure_ascii=False, indent=1))
