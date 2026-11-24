"""After build_argon_pak.py: carry the edits made to the game's script.pak after the 11-01 build over to a new build,
and point the pre-battle save prompt at its own buttons. 11-05 Claude.

  python -I -X utf8 post_build.py <new build.pak> <11-01 build.pak> <game script.pak> <out.pak>

1. Files where the game's script differs from the 11-01 build (14 files: the continue screens' voice waits, 11-03)
   get the same hunks applied to the new build. Every hunk must be found exactly once, or nothing is written.
2. (off) The save prompt shares the continue screen's YES/NO buttons, which GPT's Korean continue pack had turned into
   「이어하기/끝내기」. A first fix gave the save prompt its own 저장하기/하지 않기 buttons; the user then chose the
   original YES/NO for both (11-05 「그냥 예스, 노로 만들자」), so 0continue_ko.pak now holds the original pictures
   (khd-runtime-fix\\work\\save_buttons\\restore_yes_no.py) and the script keeps the original paths.
   --save-buttons turns the separate buttons back on.
3. (gameover_first) the enemy's GAME OVER line before the continue prompt (11-05).
4. (retry_last, 11-09; --no-retry-last leaves it out) user 「지금은 엔딩을 보면 더블 레무리아 임팩트로 끝내잖아? 이 엔딩이
   나올경우 마지막에 메인으로 돌아가기 전에 선택지가 나오게 해. 다시 마지막 전투로 돌아가겠습니까? 예스를 누를경우 이 전투에선
   자동 전투로 이길 경우 샤이닝 트라페조헤드론을 쓰도록」. The last battle (da1401, stage 15) branches on its finisher:
   FinishTech 4 = the Shining Trapezohedron (AM014 AUTO_EVENT3a, #105 = 4) -> #共通 -> Da1501a (神話フラグ, the true
   ending), anything else -> #未使用勝利 -> Da1504 -> Da1505's #無輝 ending, which closes on the LAST BATTLE picture
   showing that very input (↑↓↑+攻). There da1505 now asks 「다시 마지막 전투로 돌아가겠습니까?」 (picture
   cg\\system\\retry\\retry_back.png from 0retry_last.pak, khd-hero-ai\\retry_last\\make_retry_prompt.py; the continue
   screen's YES/NO buttons): NO goes on to the title as before, YES sets the story variable TrapezoRetry=1 and enters
   da1401 at the new #TrapezoRetry, just before its battle (SYSTEM MENU #002 as on the way there; the save and skip
   prompts are left out). da1401 sets TrapezoRetry=0 when its battle is over (#共通, #未使用勝利) or given up
   (#タイトルへ). The story auto battle's BATTLE-16 AI (autobattle\\battles\\B16) reads TrapezoRetry from system.dll's
   variable objects and, when it is 1, finishes with 8, 2, 8, A instead of X+Z. Not in the replay gallery (RPmode=1).
"""
import difflib
import importlib.util
import json
import re
import shutil
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
spec = importlib.util.spec_from_file_location("knew", r"P:\AI\khd-runtime-fix\tools\kishinhisho_pak_fixed.py")
K = importlib.util.module_from_spec(spec)
sys.modules["knew"] = K
spec.loader.exec_module(K)

SAVE_CHOICES = {
    "#ほぞんほぞんっと": ("yes", "save"),
    "#セーブ終了２": ("no", "nosave"),
}
STORY_SUBS = Path(r"F:\AI\khd-latest-game\autobattle\story_subs.tsv")      # voice lengths (seconds)
GAMEOVER = re.compile(
    r'(<MARKER NAME="#タイトルへ">\r+<clear>\r+<WIPE EFFECT="fade" TIME="30">\r+)'
    r'<WAIT TIME="10">\r+(?:<BOX type="11">\r+)?(<VOICE NAME="[^"]*" SRC="([^"]*)">)\r+'
    r'<BACKGROUND SRC="system\\GAMEOVER">\r+<WIPE EFFECT="fade" TIME="30">\r+(?:<BOX type="11">\r+)?'
    r'<WAIT TIME="50">\r+<clear>\r+<WIPE EFFECT="fade" TIME="30">\r+(?:<WAIT TIME="50">\r+)?')
GAMEOVER_SILENT = re.compile(        # da0100: the first battle's GAME OVER has no enemy line - left as it is
    r'<MARKER NAME="#タイトルへ">\r+<clear>\r+<WIPE EFFECT="fade" TIME="30">\r+<WAIT TIME="10">\r+'
    r'<BACKGROUND SRC="system\\GAMEOVER">\r+')
CONT0 = '<MARKER NAME="#コンティニュー０">\r'

# 4. retry_last (11-09)
RETRY_VAR = "TrapezoRetry"
RETRY_HINT = '<BACKGROUND SRC="system/LAST BATTLE">\r<WIPE EFFECT="fade" TIME="7">\r　\r\r'   # da1505 #無輝: the ↑↓↑+攻 picture
RETRY_REST = ('<CLEAR>\r<WIPE EFFECT="fade" TIME="30">\r\r<CALC WORKNAME="クリア" OPERATOR="true" mode="global">\r\r'
              '<A HREF="dh_ex_story.nps#RET" OPERATOR="RPmode=1"><A HREF="dh_menu.nps" OPERATOR="">\r')
_YES = "cg\\system\\continue\\select-yes-01.png;cg\\system\\continue\\select-yes-02.png;cg\\system\\continue\\select-yes-03.png"
_NO = "cg\\system\\continue\\select-no-01.png;cg\\system\\continue\\select-no-02.png;cg\\system\\continue\\select-no-03.png"
_OFF = ('<LAYER SRC="" TRANSPARENT="60" NUM="9" X="0" Y="0" ZOOM="100" SHADE="0">\r'
        '<LAYER SRC="" TRANSPARENT="100" NUM="10" X="0" Y="0" ZOOM="100" SHADE="0">\r')
RETRY_PROMPT = (                     # goes between RETRY_HINT and RETRY_REST
    '<A HREF="#RetryLastNo" OPERATOR="RPmode=1">\r'
    '<CALC WORKNAME="クリア" OPERATOR="true" mode="global">\r'
    '<LAYER SRC="#000000" TRANSPARENT="60" NUM="9" X="0" Y="0" ZOOM="100" SHADE="0">\r'
    '<LAYER SRC="system\\retry\\retry_back" TRANSPARENT="100" NUM="10" X="0" Y="0" ZOOM="100" SHADE="0">\r'
    f'<CHOICE HREF="#RetryLastYes"   FILE="{_YES}" x="0" y="286">\r'
    f'<CHOICE HREF="#RetryLastNo"   FILE="{_NO}" x="0" y="330">\r'
    '<WIPE EFFECT="fade" TIME="10">\r'
    '<SELECT>\r'
    '\r'
    '<MARKER NAME="#RetryLastYes">\r' + _OFF +
    '<CLEAR>\r'
    '<WIPE EFFECT="fade" TIME="30">\r'
    f'<CALC WORKNAME="{RETRY_VAR}" OPERATOR="1">\r'
    f'<A HREF="Da1401.nps#{RETRY_VAR}" OPERATOR="">\r'
    '\r'
    '<MARKER NAME="#RetryLastNo">\r' + _OFF +
    '\r')
RETRY_ENTRY_AT = '<SELECT>\r\r<MARKER NAME="#セーブ終了">\r'                 # da1401: after the 2nd-round skip prompt
RETRY_ENTRY = f'<MARKER NAME="#{RETRY_VAR}">\r<SYSTEM MENU="#002">\r\r'       # reached only by the jump from da1505
RETRY_RESET = f'<CALC WORKNAME="{RETRY_VAR}" OPERATOR="0">\r'
RETRY_RESET_AT = ['<MARKER NAME="#共通">\r', '<MARKER NAME="#未使用勝利">\r', '<MARKER NAME="#タイトルへ">\r']


def voice_seconds():
    out = {}
    for row in STORY_SUBS.read_text(encoding="utf-8").splitlines()[1:]:
        c = row.split("\t")
        out[c[3].lower().removeprefix("voice\\").removesuffix(".ogg")] = float(c[4])
    return out


def gameover_first(raw: bytes, name: str, secs: dict) -> tuple[bytes, bool]:
    """3. (user 11-05 「패배 대사가 출력된 이후 적의 대사까지 나온 이후 컨티뉴 화면이 뜨는거야」) The enemy's line on the
    GAME OVER picture used to come only after 「끝내기」 (#タイトルへ). Show it right after the defeat line, before the
    continue / skip prompt (#コンティニュー０), and wait for the voice to end; 「끝내기」 then goes straight to the title."""
    t = raw.decode("cp932")
    m = GAMEOVER.search(t)
    if not m:
        if "system\\GAMEOVER" in t and not GAMEOVER_SILENT.search(t):
            raise SystemExit(f"{name}: GAME OVER block has another shape")
        return raw, False
    if t.count(CONT0) != 1:
        raise SystemExit(f"{name}: {t.count(CONT0)} #コンティニュー０ markers")
    voice_tag, src = m.group(2), m.group(3)
    d = secs.get(src.lstrip("\\").lower())
    if d is None:
        raise SystemExit(f"{name}: no length for {src}")
    # the voice starts with the 3 s fade to the GAME OVER picture (WIPE waits), then wait out the rest + half a second
    wait = max(10, int(-(-(d - 3.0) * 10 // 1)) + 5)
    show = ('<clear>\r<WIPE EFFECT="fade" TIME="30">\r<WAIT TIME="10">\r<BOX type="11">\r' + voice_tag + '\r'
            '<BACKGROUND SRC="system\\GAMEOVER">\r<WIPE EFFECT="fade" TIME="30">\r'
            f'<WAIT TIME="{wait}" KEYSKIP="FALSE" SYNC="TRUE">\r\r')
    t = t[:m.start()] + m.group(1) + t[m.end():]                      # 「끝내기」: fade out, then the title as before
    t = t.replace(CONT0, CONT0 + show)
    return t.encode("cp932"), True


def retry_last(raw: bytes, name: str) -> tuple[bytes, str]:
    """4. The prompt after the Lemuria ending and the way back into the last battle (see the top). Works on the bytes
    (no cp932 round trip); every anchor must be there exactly once, or nothing is written."""
    base = name.rsplit("\\", 1)[-1].lower()
    if base not in ("da1505.nps", "da1401.nps"):
        return raw, ""
    b = lambda s: s.encode("cp932")
    if b(RETRY_VAR) in raw:
        return raw, "present"            # a game script that already has it was ported in
    if base == "da1505.nps":
        at = b(RETRY_HINT + RETRY_REST)
        if raw.count(at) != 1:
            raise SystemExit(f"{name}: the LAST BATTLE close found {raw.count(at)} times")
        return raw.replace(at, b(RETRY_HINT + RETRY_PROMPT + RETRY_REST)), "prompt"
    at = b(RETRY_ENTRY_AT)
    if raw.count(at) != 1:
        raise SystemExit(f"{name}: the way into the battle found {raw.count(at)} times")
    out = raw.replace(at, b('<SELECT>\r\r' + RETRY_ENTRY + '<MARKER NAME="#セーブ終了">\r'))
    for m in RETRY_RESET_AT:
        if out.count(b(m)) != 1:
            raise SystemExit(f"{name}: {m.strip()} found {out.count(b(m))} times")
        out = out.replace(b(m), b(m + RETRY_RESET))
    return out, "entry+reset"


def load(path):
    a = K.parse_archive(Path(path))
    return a, {e.name: K.decode_stored(e, K.read_stored(a, e)) for e in a.entries}


def port(new: bytes, old: bytes, game: bytes, name: str) -> bytes:
    a, b, n = old.split(b"\r"), game.split(b"\r"), new.split(b"\r")
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    out = b"\r".join(n)
    for tag, i1, i2, j1, j2 in reversed(sm.get_opcodes()):
        if tag == "equal":
            continue
        lo, hi = max(0, i1 - 2), min(len(a), i2 + 2)          # two lines of context on each side
        before = b"\r".join(a[lo:hi])
        after = b"\r".join(a[lo:i1] + b[j1:j2] + a[i2:hi])
        if out.count(before) != 1:
            raise SystemExit(f"{name}: hunk at line {i1} found {out.count(before)} times in the new build")
        out = out.replace(before, after)
    return out


def save_buttons(raw: bytes, name: str) -> tuple[bytes, int]:
    t = raw.decode("cp932")
    n = 0

    def rep(m):
        nonlocal n
        href, files = m.group(1), m.group(2)
        kind, new = SAVE_CHOICES[href]
        want = ";".join(f"cg\\system\\continue\\select-{kind}-0{i}.png" for i in (1, 2, 3))
        if files != want:
            raise SystemExit(f"{name}: save CHOICE {href} has FILE={files}")
        n += 1
        return m.group(0).replace(files, ";".join(f"cg\\system\\save\\select-{new}-0{i}.png" for i in (1, 2, 3)))

    t = re.sub(r'<CHOICE HREF="(#ほぞんほぞんっと|#セーブ終了２)"\s+FILE="([^"]*)"', rep, t)
    return t.encode("cp932"), n


def main():
    new_pak, old_pak, game_pak, out_pak = (Path(x) for x in [a for a in sys.argv[1:] if not a.startswith("--")][:4])
    _, NEW = load(new_pak)
    _, OLD = load(old_pak)
    _, GAME = load(game_pak)
    work = out_pak.parent / (out_pak.stem + "_files")
    if work.exists():
        shutil.rmtree(work)
    log = {"ported": [], "save_choices": {}, "gameover_first": [], "retry_last": {}}
    secs = voice_seconds()
    for name, raw in NEW.items():
        res = raw
        if GAME[name] != OLD[name]:
            res = port(res, OLD[name], GAME[name], name)
            log["ported"].append(name)
        if "--save-buttons" in sys.argv and "セーブ台座".encode("cp932") in res:
            res, k = save_buttons(res, name)
            if k:
                log["save_choices"][name] = k
        res, moved = gameover_first(res, name, secs)
        if moved:
            log["gameover_first"].append(name)
        if "--no-retry-last" not in sys.argv:
            res, what = retry_last(res, name)
            if what:
                log["retry_last"][name] = what
        if res != GAME[name]:
            dst = work / Path(name.replace("\\", "/"))
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(res)
    K.build_archive(game_pak, work, out_pak)
    _, BACK = load(out_pak)
    for name in NEW:
        want = (work / Path(name.replace("\\", "/"))).read_bytes() if (work / Path(name.replace("\\", "/"))).exists() else GAME[name]
        assert BACK[name] == want, name
    log["changed_files"] = sum(1 for p in work.rglob("*") if p.is_file())
    log["save_prompts"] = sum(log["save_choices"].values())
    log["out_sha256"] = K.sha256_file(out_pak)
    (out_pak.with_suffix(".post.json")).write_text(json.dumps(log, ensure_ascii=False, indent=1), encoding="utf-8")
    print(json.dumps({k: (v if k not in ("save_choices", "ported") else len(v)) for k, v in log.items()}, ensure_ascii=False))


if __name__ == "__main__":
    main()
