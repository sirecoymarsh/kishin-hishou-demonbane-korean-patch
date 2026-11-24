"""Korean install path test for Kishin Hisho (11-16 Claude). Reuses P:\AI\khd-hero-ai\fullrun\fullrun.py
(launch on the left vertical monitor without activation, PRESS START, settings walk, title 시작하기, quit, restore ini/registry).
  python -I -X utf8 kp_test.py <game dir> <label> sysinfo,battle
sysinfo: title → 환경설정, every tab incl. 시스템 정보 (the 10-30 fix shows engine 1.20 there), then close.
battle : title 시작하기 → story (helper skips) → first story fight; pass = fight loaded and the game alive 75 s into it.
"""
import sys, time, json, ctypes, ctypes.wintypes as W, re, subprocess
from pathlib import Path
sys.path.insert(0, r"P:\AI\khd-hero-ai\fullrun")
import fullrun as F
S = F.S
game = Path(sys.argv[1]); label = sys.argv[2]; steps = sys.argv[3].split(",")
HERE = Path(r"P:\AI\khd-hero-ai\v30\runs"); HERE.mkdir(exist_ok=True)
F.G = game; F.EXE = game / "KishinHisho_chs.exe"; F.AB = game / "autobattle"
S.GAME = game; S.INI = F.AB / "autobattle.ini"
F.HERE = HERE; F.STATE = HERE / f"state_{label}.json"
def _other_games(own_pid):
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
        "[Console]::OutputEncoding=[Text.Encoding]::UTF8; Get-Process KishinHisho_chs -ErrorAction SilentlyContinue | ForEach-Object { \"$($_.Id)`t$($_.Path)\" }"],
        capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    return [ln.strip() for ln in out.splitlines() if "	" in ln and ln.split("	",1)[1].strip().lower() != str(F.EXE).lower() and int(ln.split("	",1)[0]) != own_pid]
F.other_games = _other_games
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = W.HANDLE
res = {"label": label, "game": str(game), "steps": steps, "checks": {}}
def code_of(hp):
    c = W.DWORD(); k32.GetExitCodeProcess(hp, ctypes.byref(c)); return c.value
def dumps():
    return sorted(p.name for p in game.glob("*.crash.dmp"))
def vc_error_box(pid):
    for h, p in S.game_windows("#32770"):
        b = ctypes.create_unicode_buffer(256); S.u.GetWindowTextW(h, b, 256)
        if p == pid and S.u.IsWindowVisible(h) and "Runtime" in b.value:
            return h, b.value
    return None, ""
import threading, realreg
HIDE = "--visible" not in sys.argv
OFF = (20000, 0)
user_pos = None
if HIDE:   # user 11-16 「안보이게 테스트해줘」: start off screen, mute, no subtitles, keep every game window off screen
    reg = realreg.query(); user_pos = (reg.get("XPos"), reg.get("YPos"))
    realreg.set_dword("XPos", OFF[0]); realreg.set_dword("YPos", OFF[1])
    F.POS_BY_MON["3"] = OFF
    sys.argv += ["--ini=mute=1", "--ini=subtitles=0"]
    stop_mover = threading.Event(); ours = {}; moved = []
    def mover():
        cb_t = S.WNDENUMPROC
        while not stop_mover.is_set():
            def cb(hw, _l):
                if not S.u.IsWindowVisible(hw): return True
                p = W.DWORD(); S.u.GetWindowThreadProcessId(hw, ctypes.byref(p)); p = p.value
                if p not in ours:
                    info = F.proc_info(p); ours[p] = bool(info) and info[0].lower() == str(F.EXE).lower() and info[1] >= t_start - 2
                if ours[p]:
                    rc = W.RECT(); S.u.GetWindowRect(hw, ctypes.byref(rc))
                    if rc.left < 15000:
                        S.u.SetWindowPos(hw, 1, OFF[0] + 900, OFF[1], 0, 0, 0x0001 | 0x0010)   # HWND_BOTTOM, NOSIZE|NOACTIVATE
                        b = ctypes.create_unicode_buffer(64); S.u.GetClassNameW(hw, b, 64); moved.append((round(time.time() - t_start, 2), b.value, rc.left, rc.top))
                return True
            S.u.EnumWindows(cb_t(cb), 0)
            time.sleep(0.02)
dumps0 = dumps(); t_start = time.time()
run_log_off = F.size(F.AB / "proxy_log.txt")
if HIDE: threading.Thread(target=mover, daemon=True).start()
F.cmd_start()
st = F.load_state(); out = Path(st["out"]); pid = st["pid"]; h = st["hwnd"]
log = F.Log(out); res["out"] = str(out); res["pid"] = pid
hp = k32.OpenProcess(0x1000 | 0x100000, False, pid)
try:
    if "sysinfo" in steps:
        F.cmd_settings("list")
        time.sleep(2)
        eh, et = vc_error_box(pid)
        txt = (out / "settings_controls.txt").read_text(encoding="utf-8") if (out / "settings_controls.txt").exists() else ""
        res["checks"]["sysinfo_error_box"] = et
        res["checks"]["sysinfo_shows_1.20"] = "1.20" in txt
        res["checks"]["alive_after_settings"] = F.alive(pid)
        if eh:
            F.shot(eh, out, "sysinfo_error_box")
            log(f"error box after settings: {et}")
            S.u.PostMessageW(eh, 0x0010, 0, 0); time.sleep(3)
        log(f"sysinfo: shows 1.20 {res['checks']['sysinfo_shows_1.20']}, error box {et!r}, alive {F.alive(pid)}")
    # first start of a fresh install: the game opens its settings window once (key setup) - close it with 닫기 (IDCANCEL)
    for _ in range(40):
        sd = [hh for hh, pp in S.game_windows("#32770") if pp == pid and S.u.IsWindowVisible(hh) and S.u.GetDlgItem(hh, 0x3ED)]
        if sd:
            res["checks"]["first_run_settings_closed"] = True; log("settings window at the start - 닫기")
            S.u.PostMessageW(sd[0], 0x0111, 2, 0); time.sleep(2); break
        time.sleep(0.25)
    if "screens" in steps and F.alive(pid):
        # v30: the 감상실 screens one after another (title -> EXTRA -> each item, shot, EXIT back)
        def click(x, y, w):
            F.gclick(h, x, y); time.sleep(w)
        F.shot(h, out, "s00_title")
        click(405, 416, 5); F.shot(h, out, "s01_extra")
        for name, y in (("cg", 117), ("ff", 221), ("duel", 273), ("story", 325), ("theater", 377), ("finish", 429)):
            click(400, y, 6); F.shot(h, out, f"s_{name}")
            click(740, 550, 4)
        F.shot(h, out, "s99_back")
        res["checks"]["screens_alive"] = F.alive(pid)
    if "uatoggle" in steps and F.alive(pid):
        # v30: 환경설정 -> 기본 page box 0x5A20 「모든 콘텐츠 열기」 -> the question -> 예 / just untick; then 감상실
        def find_ctl(cid):
            for hh, pp in S.game_windows("#32770"):
                if pp != pid or not S.u.IsWindowVisible(hh): continue
                found = []
                def ecb(c, _l):
                    if S.u.GetDlgCtrlID(c) == cid: found.append(c)
                    return True
                S.u.EnumChildWindows(hh, S.WNDENUMPROC(ecb), 0)
                if found: return hh, found[0]
            return None, None
        def settings_open():
            F.gclick(h, 405, 442); time.sleep(4)
            return find_ctl(0x5A20)
        def answer(yes):
            for _ in range(30):
                for hh, pp in S.game_windows("#32770"):
                    tb = ctypes.create_unicode_buffer(128); S.u.GetWindowTextW(hh, tb, 128)
                    if pp == pid and S.u.IsWindowVisible(hh) and tb.value == "모든 콘텐츠 열기":
                        F.shot(hh, out, "ua_question")
                        S.u.PostMessageW(S.u.GetDlgItem(hh, 6 if yes else 7), 0x00F5, 0, 0); return True
                time.sleep(0.2)
            return False
        for turn, want in (("on", True), ("off", False)):
            dlg, box = settings_open()
            res["checks"][f"ua_box_{turn}"] = bool(box)
            if not box: log("no 모든 콘텐츠 열기 box"); break
            F.shot(dlg, out, f"ua_settings_{turn}_before")
            S.u.PostMessageW(box, 0x00F5, 0, 0)            # BM_CLICK
            if want: res["checks"]["ua_question"] = answer(True)
            time.sleep(1.5)
            res["checks"][f"ua_checked_{turn}"] = S.u.SendMessageW(box, 0x00F0, 0, 0)   # BM_GETCHECK
            F.shot(dlg, out, f"ua_settings_{turn}_after")
            S.u.PostMessageW(dlg, 0x0111, 2, 0); time.sleep(2)  # 닫기
            F.gclick(h, 405, 416); time.sleep(5); F.shot(h, out, f"ua_extra_{turn}")
            F.gclick(h, 740, 550); time.sleep(4)
    if "movie" in steps and F.alive(pid):
        # release test: new game and let the opening play 40 s (which movie pack the engine reads shows in a trace build)
        F.cmd_newgame()
        for k in range(8):
            time.sleep(5); F.shot(h, out, f"movie_{(k + 1) * 5:02d}s")
        res["checks"]["movie_alive_40s"] = F.alive(pid)
    if "prompt" in steps and F.alive(pid):
        # v29: T on a text screen (accelerator T -> 0x9c59) -> the engine's yes/no prompt; its static 0x45C must be Korean
        F.cmd_newgame()
        t0 = time.time(); got = None
        def prompt_box():
            for hh, pp in S.game_windows("#32770"):
                if pp == pid and S.u.IsWindowVisible(hh) and S.u.GetDlgItem(hh, 0x45C):
                    return hh
            return None
        while time.time() - t0 < 90 and F.alive(pid) and not got:
            S.u.PostMessageW(h, 0x100, 0x54, 0x00140001); time.sleep(0.05); S.u.PostMessageW(h, 0x101, 0x54, 0xC0140001)
            for _ in range(20):
                dh = prompt_box()
                if dh: got = dh; break
                time.sleep(0.05)
        if got:
            tb = ctypes.create_unicode_buffer(256); S.u.GetWindowTextW(S.u.GetDlgItem(got, 0x45C), tb, 256)
            cb = ctypes.create_unicode_buffer(256); S.u.GetWindowTextW(got, cb, 256)
            res["checks"]["prompt_text"] = tb.value; res["checks"]["prompt_title"] = cb.value
            res["checks"]["prompt_after_s"] = round(time.time() - t0, 1)
            log(f"prompt: {tb.value!r} title {cb.value!r}")
            time.sleep(0.5); F.shot(got, out, "prompt")
            btns = []
            def ecb(c, _l):
                b2 = ctypes.create_unicode_buffer(32); S.u.GetClassNameW(c, b2, 32)
                if b2.value.lower() == "button": btns.append(c)
                return True
            S.u.EnumChildWindows(got, S.WNDENUMPROC(ecb), 0)
            names = []
            for c in btns:
                b3 = ctypes.create_unicode_buffer(64); S.u.GetWindowTextW(c, b3, 64); names.append(b3.value)
            res["checks"]["prompt_buttons"] = names
            no = btns[-1] if btns else None
            if no: S.u.PostMessageW(no, 0x00F5, 0, 0)          # BM_CLICK the last button (아니요)
            time.sleep(2)
            res["checks"]["prompt_closed"] = prompt_box() is None
            res["checks"]["alive_after_prompt"] = F.alive(pid)
        else:
            res["checks"]["prompt_text"] = None
            log("prompt: no dialog came up within 90 s")
    if "battle" in steps and F.alive(pid):
        off = F.size(F.AB / "proxy_log.txt"); aoff = F.size(F.AB / "adv_log.tsv")
        if "prompt" not in steps: F.cmd_newgame()
        fight_at = None; t0 = time.time(); seen = []
        while time.time() - t0 < 300 and F.alive(pid):
            lines, off = F.new_lines(F.AB / "proxy_log.txt", off)
            for ln in lines:
                m = re.search(r"fight kind: (\w[\w ]*?) \(scene (\d+)", ln)
                if m and fight_at is None:
                    fight_at = time.time(); res["checks"]["fight"] = f"{m[1]} scene {m[2]}"; log(f"fight started: {m[1]} scene {m[2]}")
            al, aoff = F.new_lines(F.AB / "adv_log.tsv", aoff)
            for ln in al:
                b = ln.split("\t", 1)[-1]
                if b.startswith(("fight over", "prompt", "screen: gameover")): seen.append(b[:120]); log("helper: " + b[:120])
            if fight_at and time.time() - fight_at > 20 and "shot20" not in res["checks"]:
                res["checks"]["shot20"] = True; F.shot(h, out, "fight_20s")
            if fight_at and time.time() - fight_at > 75:
                F.shot(h, out, "fight_75s"); break
            time.sleep(2)
        res["checks"]["helper_lines"] = seen
        res["checks"]["fight_alive_75s"] = bool(fight_at) and F.alive(pid) and time.time() - fight_at >= 75
        if not F.alive(pid): log("game process ended during the battle step")
    if "quit" in steps and F.alive(pid):
        # v29b: closing the window asks the game's own question - its static 0x45C must be Korean; answer 예
        S.u.PostMessageW(h, 0x0010, 0, 0)
        t0 = time.time(); qb = None
        while time.time() - t0 < 15 and not qb:
            for hh, pp in S.game_windows("#32770"):
                if pp == pid and S.u.IsWindowVisible(hh) and S.u.GetDlgItem(hh, 0x45C):
                    qb = hh
            time.sleep(0.1)
        if qb:
            tb = ctypes.create_unicode_buffer(256); S.u.GetWindowTextW(S.u.GetDlgItem(qb, 0x45C), tb, 256)
            res["checks"]["quit_text"] = tb.value; log(f"quit question: {tb.value!r}")
            time.sleep(0.5); F.shot(qb, out, "quit")
            bl = []
            def qcb(c, _l):
                b4 = ctypes.create_unicode_buffer(32); S.u.GetClassNameW(c, b4, 32)
                if b4.value.lower() == "button": bl.append(c)
                return True
            S.u.EnumChildWindows(qb, S.WNDENUMPROC(qcb), 0)
            if bl: S.u.PostMessageW(bl[0], 0x00F5, 0, 0)
            for _ in range(60):
                if not F.alive(pid): break
                time.sleep(0.5)
            res["checks"]["quit_exited"] = not F.alive(pid)
        else:
            res["checks"]["quit_text"] = None
    if "quit2" in steps and F.alive(pid):
        # v29c: close the window during the first fight (battle module box) -> No; after the fight (story, engine question) -> Yes
        def any_box():
            for hh, pp in S.game_windows("#32770"):
                if pp == pid and S.u.IsWindowVisible(hh):
                    texts, btns = [], []
                    def ecb(c, _l):
                        cl = ctypes.create_unicode_buffer(32); S.u.GetClassNameW(c, cl, 32)
                        tx = ctypes.create_unicode_buffer(512); S.u.GetWindowTextW(c, tx, 512)
                        if cl.value.lower() == "button": btns.append((c, tx.value))
                        elif cl.value.lower() == "static" and tx.value: texts.append(tx.value)
                        return True
                    S.u.EnumChildWindows(hh, S.WNDENUMPROC(ecb), 0)
                    cap = ctypes.create_unicode_buffer(256); S.u.GetWindowTextW(hh, cap, 256)
                    if texts: return hh, cap.value, texts, btns
            return None
        def close_and_answer(tag, which):
            S.u.PostMessageW(h, 0x0010, 0, 0)
            t1 = time.time(); got = None
            while time.time() - t1 < 20 and not got and F.alive(pid):
                got = any_box(); time.sleep(0.2)
            if not got:
                res["checks"][tag] = None; log(f"{tag}: no box"); return
            hh, cap, texts, btns = got
            res["checks"][tag] = dict(caption=cap, texts=texts, buttons=[b2 for _c, b2 in btns])
            log(f"{tag}: {cap!r} {texts!r} {[b2 for _c, b2 in btns]!r}")
            time.sleep(0.5); F.shot(hh, out, tag)
            if btns: S.u.PostMessageW(btns[which][0] if which < len(btns) else btns[-1][0], 0x00F5, 0, 0)
        off = run_log_off; fight_at = None; t0 = time.time()   # from the start of this run (the fight may begin during the prompt step)
        if "prompt" not in steps and "battle" not in steps: F.cmd_newgame()
        while time.time() - t0 < 300 and F.alive(pid) and fight_at is None:
            lines, off = F.new_lines(F.AB / "proxy_log.txt", off)
            if any("fight kind:" in ln for ln in lines): fight_at = time.time()
            time.sleep(1)
        if fight_at:
            time.sleep(12)
            close_and_answer("box_in_fight", 1)                  # 아니요: keep playing
            time.sleep(3); res["checks"]["alive_after_fight_no"] = F.alive(pid)
            aoff = 0; t2 = time.time(); over = False
            while time.time() - t2 < 240 and F.alive(pid) and not over:
                lines, off = F.new_lines(F.AB / "proxy_log.txt", off)
                over = any("fight over" in ln for ln in lines)
                time.sleep(1)
            time.sleep(10)
            close_and_answer("box_in_story", 0)                  # 예: quit
            for _ in range(60):
                if not F.alive(pid): break
                time.sleep(0.5)
            res["checks"]["quit_exited"] = not F.alive(pid)
    if "fullrun" in steps and F.alive(pid):
        F.cmd_newgame()
        F.cmd_watch(1.6)
        st2 = F.load_state(); res["checks"]["fullrun_verdict"] = st2.get("verdict"); res["checks"]["fullrun_stages"] = st2.get("stages")
        pid = st2.get("pid", pid)
finally:
    alive = F.alive(pid)
    if alive:
        hh, _ = F.our_window(); F.quit_game(hh or h, pid, log)
    time.sleep(1)
    # v30: a dialog left open (load list, the exit question) keeps the game alive - close them and answer 예 to the question,
    # then wait for the exit (a game closed later writes its off-screen window position over the user's)
    for _ in range(40):
        if not F.alive(pid): break
        for hh, pp in S.game_windows("#32770"):
            if pp != pid or not S.u.IsWindowVisible(hh): continue
            if S.u.GetDlgItem(hh, 1116) or S.u.GetDlgItem(hh, 0x45C):
                S.u.PostMessageW(S.u.GetDlgItem(hh, 1) or S.u.GetDlgItem(hh, 6), 0x00F5, 0, 0)
            else:
                S.u.PostMessageW(hh, 0x0111, 2, 0)
        if not any(pp == pid and S.u.IsWindowVisible(hh) for hh, pp in S.game_windows("#32770")):
            S.u.PostMessageW(h, 0x0010, 0, 0)
        time.sleep(1)
    res["exit_code"] = hex(code_of(hp)) if hp else None
    res["new_dumps"] = [d for d in dumps() if d not in dumps0]
    if not F.alive(pid):
        F.restore(st, log)
    if HIDE:
        stop_mover.set(); time.sleep(0.1)
        res["moved_windows"] = moved[:20]
        if user_pos[0] is not None:
            realreg.set_dword("XPos", user_pos[0]); realreg.set_dword("YPos", user_pos[1])
        r2 = realreg.query(); res["window_pos_restored"] = (r2.get("XPos"), r2.get("YPos")) == user_pos
        log(f"window position put back to {user_pos}: {res['window_pos_restored']}")
    res["nvlddmkm_before_after"] = (st.get("nv0"), F.nv_count(st["t0"] - 1))
    res["seconds"] = round(time.time() - t_start)
    (out / "kp_result.json").write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding="utf-8")
    print(json.dumps(res, ensure_ascii=False, indent=1))
