# 11-18 밤 사용자 「본패치만 / 본패치+4K / 본패치+4K+책 한번씩 띄워줄래? 확인해보게」
# 깨끗한 1.20(testbase\기신비상 깨끗한 판, 읽기만) 을 통째로 세 벌 복사(하드 링크 아님) → 배포 zip 과 같은 dist 폴더의 설치기로 설치.
$ErrorActionPreference = 'Stop'
$root = 'Y:\AI\claude-work\khd-release-1118'
$src  = "$root\testbase\기신비상 깨끗한 판"
$dst  = "$root\check3"
$core = "$root\dist\KishinHisho_KO_20261118\Patch.ps1"
$hd   = "$root\dist\KishinHisho_KO_20261118_HD\Patch.ps1"
$nov  = "$root\dist\KishinHisho_KO_20261118_Novels\Patch.ps1"
$sets = [ordered]@{
    '1 본패치만'       = @($core)
    '2 본패치+4K'      = @($core, $hd)
    '3 본패치+4K+책'   = @($core, $hd, $nov)
}
New-Item -ItemType Directory -Force $dst | Out-Null
foreach ($name in $sets.Keys) {
    $g = Join-Path $dst $name
    if (Test-Path -LiteralPath $g) { throw "이미 있음: $g" }
    "== $name : 복사"
    robocopy $src $g /E /COPY:DAT /R:0 /W:0 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy 실패 $LASTEXITCODE" }
    foreach ($p in $sets[$name]) {
        "-- 설치: $(Split-Path (Split-Path $p) -Leaf)"
        $out = & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $p -Mode Install -GamePath $g 2>&1 | Out-String -Width 300
        $out.TrimEnd()
        "   (exit $LASTEXITCODE)"
    }
}
"끝"
