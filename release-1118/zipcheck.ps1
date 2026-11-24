# 11-19 압축형 배포판 확인: 깨끗한 1.20(testbase\기신비상 깨끗한 판, 읽기만)을 두 벌 복사해 Windows 기본 Expand-Archive 로 풀어 덮어쓰기.
#  A = 본 패치만,  B = 소설 먼저 → 본 패치 나중(순서가 상관없는지)
$ErrorActionPreference = 'Stop'
$root = 'Y:\AI\claude-work\khd-release-1118'
$src = "$root\testbase\기신비상 깨끗한 판"
$z = "$root\dist_zip"
$out = "$root\zipcheck"
if (Test-Path -LiteralPath $out) { throw "이미 있음: $out" }
New-Item -ItemType Directory $out | Out-Null
foreach ($n in 'A','B') {
    robocopy $src "$out\$n" /E /COPY:DAT /R:0 /W:0 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy $LASTEXITCODE" }
}
$t = Get-Date
Expand-Archive -LiteralPath "$z\KishinHisho_KO_20261118.zip" -DestinationPath "$out\A" -Force
"A core: {0:N0} s" -f ((Get-Date) - $t).TotalSeconds
$t = Get-Date
Expand-Archive -LiteralPath "$z\KishinHisho_KO_20261118_Novels.zip" -DestinationPath "$out\B" -Force
Expand-Archive -LiteralPath "$z\KishinHisho_KO_20261118.zip" -DestinationPath "$out\B" -Force
"B novels+core: {0:N0} s" -f ((Get-Date) - $t).TotalSeconds
foreach ($n in 'A','B') {
    $f = Get-ChildItem -LiteralPath "$out\$n" -Recurse -File
    "{0}: {1} files, {2:N2} GB" -f $n, $f.Count, (($f | Measure-Object Length -Sum).Sum / 1GB)
}
'끝'
