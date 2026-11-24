[CmdletBinding()]
param(
    [ValidateSet('Check','Install','Restore')][string]$Mode = 'Check',
    [string]$GamePath,
    [string]$BackupPath
)
# 기신비상 데몬베인 한국어 패치 설치기 (khd-release-v2, 2026-11-18)
# 바탕: 10-29 배포 설치기(백업·실패 시 되돌리기·복원). 바뀐 점: 원본 파일의 해시가 달라도 막지 않고 경고만,
# 더는 쓰지 않는 파일 치우기(remove), 서재 책 목록 합치기(books_add).
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$PackageRoot = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$Utf8 = New-Object System.Text.UTF8Encoding($false)
$Warnings = New-Object 'System.Collections.Generic.List[string]'
function Warn([string]$Text) { $Warnings.Add($Text); Write-Warning $Text }
Add-Type -Namespace KhdPatch -Name Ini -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern uint GetPrivateProfileStringW(string s, string k, string d, System.Text.StringBuilder r, uint n, string f);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern bool WritePrivateProfileStringW(string s, string k, string v, string f);
'@
# 설정 파일(UTF-16 ini)의 [섹션] 키=값 목록
function IniPairs([string]$Path) {
    $pairs=New-Object 'System.Collections.Generic.List[object]'; $sec=$null
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        $t=$line.Trim()
        if ($t -match '^\[(.+)\]$') { $sec=$Matches[1]; continue }
        if ($sec -and $t -notmatch '^;' -and $t -match '^([^=]+)=(.*)$') { $pairs.Add([pscustomobject]@{s=$sec;k=$Matches[1].Trim();v=$Matches[2]}) }
    }
    return ,$pairs
}
function IniHas([string]$Path,[string]$Section,[string]$Key) {
    $sb=New-Object System.Text.StringBuilder 512
    [void][KhdPatch.Ini]::GetPrivateProfileStringW($Section,$Key,[string][char]1,$sb,512,$Path)
    return $sb.ToString() -ne [string][char]1
}

function Hash([string]$Path) {
    if (-not [IO.File]::Exists($Path)) { return $null }
    $stream=[IO.File]::OpenRead($Path)
    $algorithm=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
function Full([string]$Path) { return [IO.Path]::GetFullPath($Path).TrimEnd('\') }
function Within([string]$Path,[string]$Root) {
    return $Path.Equals($Root,[StringComparison]::OrdinalIgnoreCase) -or $Path.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase)
}
function SafeJoin([string]$Root,[string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or ($Relative -split '[/\\]' | Where-Object { $_ -in @('..','.','') })) { throw "안전하지 않은 경로: $Relative" }
    $path = Full (Join-Path $Root $Relative)
    if (-not (Within $path $Root) -or $path -eq $Root) { throw "폴더 밖을 가리키는 경로: $Relative" }
    return $path
}
function NoLinks([string]$Path) {
    $cursor=$Path
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item=Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "연결(링크) 폴더는 지원하지 않습니다: $cursor" }
        }
        $next=[IO.Path]::GetDirectoryName($cursor)
        if ($next -eq $cursor) { break }
        $cursor=$next
    }
}
function WriteJson([string]$Path,$Value) {
    $text=$Value | ConvertTo-Json -Depth 12
    $temp=$Path+'.writing'
    [IO.File]::WriteAllText($temp,$text,$Utf8)
    if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temp,$Path,[NullString]::Value) } else { [IO.File]::Move($temp,$Path) }
}
function GameStopped([string]$Root) {
    foreach ($name in @('KishinHisho_chs','KishinHisho','KhdAutoBattle','KhdSubtitles','KhdBattle')) {
        foreach ($p in @(Get-Process -Name $name -ErrorAction SilentlyContinue)) {
            try { $path=$p.Path } catch { throw "실행 중인 $name 을(를) 확인할 수 없습니다. 게임을 끄고 다시 하세요." }
            if (-not $path) { throw "실행 중인 $name 을(를) 확인할 수 없습니다. 게임을 끄고 다시 하세요." }
            if (Within (Full $path) $Root) { throw "이 폴더의 게임이 실행 중입니다. 끄고 다시 하세요. (PID $($p.Id))" }
        }
    }
}
function PutFile([string]$Source,[string]$Destination,[string]$Expected,[string]$Tag) {
    if ($Tag -notmatch '^[0-9a-f]{32}$') { throw 'Invalid transaction tag.' }
    $parent=[IO.Path]::GetDirectoryName($Destination)
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    $temp=$Destination+'.khdincoming-'+$Tag
    if ([IO.File]::Exists($temp)) { throw "임시 파일이 이미 있습니다: $temp" }
    try {
        [IO.File]::Copy($Source,$temp,$false)
        if ((Hash $temp) -ne $Expected) { throw "복사한 파일이 다릅니다: $Destination" }
        if ([IO.File]::Exists($Destination)) { [IO.File]::Replace($temp,$Destination,[NullString]::Value) } else { [IO.File]::Move($temp,$Destination) }
        if ((Hash $Destination) -ne $Expected) { throw "설치한 파일이 다릅니다: $Destination" }
    } finally { if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) } }
}
function AfterOf($Row) { if ($Row.after) { return $Row.after } else { return $null } }
function ValidateBackup($State,[string]$Backup,[string]$Target) {
    if ($State.format -ne 'khd-backup-v1' -or (Full $State.target) -ne $Target) { throw '이 백업은 이 게임 폴더의 것이 아닙니다.' }
    if ($State.tag -notmatch '^[0-9a-f]{32}$') { throw 'Invalid transaction tag.' }
    $seen=@{}
    foreach ($row in @($State.plan)) {
        if ($seen.ContainsKey($row.path.ToLowerInvariant())) { throw 'Duplicate backup path.' }
        $seen[$row.path.ToLowerInvariant()]=$true
        if (($row.before -and $row.before -notmatch '^[0-9a-f]{64}$') -or ($row.after -and $row.after -notmatch '^[0-9a-f]{64}$')) { throw 'Invalid backup hash.' }
        $dest=SafeJoin $Target $row.path; NoLinks $dest
        NoLinks ($dest+'.khdincoming-'+$State.tag)
        $source=SafeJoin (Join-Path $Backup 'original') $row.path; NoLinks $source
        if ($row.before -and (Hash $source) -ne $row.before) { throw "백업 파일이 손상됐습니다: $($row.path)" }
        $current=Hash $dest
        $config=$row.PSObject.Properties['config'] -and $row.config     # 설정 파일(ini)은 게임이 고쳐 써도 복원한다
        if (-not $config -and $current -ne $row.before -and $current -ne (AfterOf $row)) { throw "설치 뒤에 바뀐 파일이 있습니다: $($row.path). 그 파일을 따로 보관한 뒤 복원하세요." }
    }
}
function RestorePlan($State,[string]$Backup,[string]$Target) {
    ValidateBackup $State $Backup $Target
    $reverse=@($State.plan); [array]::Reverse($reverse)
    foreach ($row in $reverse) {
        $dest=SafeJoin $Target $row.path
        $incoming=$dest+'.khdincoming-'+$State.tag
        if ([IO.File]::Exists($incoming)) { [IO.File]::Delete($incoming) }
        if ((Hash $dest) -ne $row.before) {
            if ($row.before) { PutFile (SafeJoin (Join-Path $Backup 'original') $row.path) $dest $row.before $State.tag }
            elseif ([IO.File]::Exists($dest)) { [IO.File]::Delete($dest) }
        }
        if ((Hash $dest) -ne $row.before) { throw "복원 확인 실패: $($row.path)" }
    }
    foreach ($relative in @($State.created_directories | Sort-Object Length -Descending)) {
        $dir=SafeJoin $Target $relative
        if ([IO.Directory]::Exists($dir) -and @([IO.Directory]::EnumerateFileSystemEntries($dir)).Count -eq 0) { [IO.Directory]::Delete($dir) }
    }
}
# 서재 책 목록(ko-library\books.ini, UTF-16): 책 칸을 원래 순서로 앞에 끼워 넣고 이미 있는 칸(같은 Path)은 그대로
function ReadBooks([string]$Path) {
    $books=New-Object 'System.Collections.Generic.List[object]'
    if (-not [IO.File]::Exists($Path)) { return ,$books }
    $section=$null; $cur=$null
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        $t=$line.Trim()
        if ($t -match '^\[(.+)\]$') {
            if ($cur -ne $null) { $books.Add($cur) }
            $section=$Matches[1]; $cur=$null
            if ($section -match '^Book\d+$') { $cur=[ordered]@{} }
            continue
        }
        if ($cur -ne $null -and $t -match '^([^=]+)=(.*)$') { $cur[$Matches[1].Trim()]=$Matches[2] }
    }
    if ($cur -ne $null) { $books.Add($cur) }
    return ,$books
}
function BooksText($Books) {
    $sb=New-Object System.Text.StringBuilder
    [void]$sb.Append("[Library]`r`nCount=$($Books.Count)`r`n")
    for ($i=0; $i -lt $Books.Count; $i++) {
        [void]$sb.Append("`r`n[Book$i]`r`n")
        foreach ($k in $Books[$i].Keys) { [void]$sb.Append("$k=$($Books[$i][$k])`r`n") }
    }
    return $sb.ToString()
}

try {
    if (-not $GamePath) { $GamePath=Read-Host '게임 폴더 경로를 넣으세요 (KishinHisho*.exe 가 있는 폴더, 끌어다 놓아도 됩니다)' }
    $GamePath=Full $GamePath.Trim().Trim('"')
    if (-not [IO.Directory]::Exists($GamePath)) { throw '게임 폴더가 없습니다.' }
    NoLinks $GamePath
    if ((Within $GamePath $PackageRoot) -or (Within $PackageRoot $GamePath)) { throw '패치는 게임 폴더 밖에 풀어 주세요.' }
    if ($Mode -ne 'Check') { GameStopped $GamePath }
    if ($Mode -eq 'Restore') {
        if (-not $BackupPath) { $BackupPath=Read-Host '백업 폴더 경로를 넣으세요 (state.json 이 있는 폴더)' }
        $BackupPath=Full $BackupPath.Trim().Trim('"'); NoLinks $BackupPath
        if ((Within $BackupPath $GamePath) -or (Within $BackupPath $PackageRoot) -or (Within $GamePath $BackupPath) -or (Within $PackageRoot $BackupPath)) { throw '백업 폴더는 게임·패치 폴더 밖이어야 합니다.' }
        $state=Get-Content -LiteralPath (Join-Path $BackupPath 'state.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        RestorePlan $state $BackupPath $GamePath
        $state.status='restored'; WriteJson (Join-Path $BackupPath 'state.json') $state
        Write-Output '복원 끝: 설치 전 파일 그대로 돌아갔습니다. 세이브는 건드리지 않았습니다.'
        exit 0
    }
    $manifest=Get-Content -LiteralPath (Join-Path $PackageRoot 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.format -ne 'khd-release-v2') { throw '이 설치기와 맞지 않는 꾸러미입니다.' }
    Write-Output "꾸러미: $($manifest.title)"
    $payload=Join-Path $PackageRoot 'files'
    NoLinks $payload
    $seen=@{}
    Write-Output '꾸러미 파일 확인 중...'
    foreach ($row in @($manifest.files)) {
        $source=SafeJoin $payload $row.path; NoLinks $source
        if ($seen.ContainsKey($row.path.ToLowerInvariant())) { throw 'Duplicate payload path.' }
        $seen[$row.path.ToLowerInvariant()]=$true
        if ((Hash $source) -ne $row.sha256 -or (Get-Item -LiteralPath $source).Length -ne $row.size) { throw "꾸러미 파일이 손상됐습니다(다시 받아 주세요): $($row.path)" }
    }
    foreach ($file in @(Get-ChildItem -LiteralPath $payload -Recurse -File -Force)) {
        $rel=$file.FullName.Substring($payload.Length+1).Replace('\','/').ToLowerInvariant()
        if (-not $seen.ContainsKey($rel)) { throw "꾸러미에 모르는 파일이 있습니다: $rel" }
    }
    # 원본 게임 파일: 없거나 다르면 경고만(막지 않음)
    foreach ($row in @($manifest.required)) {
        $file=SafeJoin $GamePath $row.path; NoLinks $file
        $h=Hash $file
        if (-not $h) { Warn "원본 게임 파일이 없습니다: $($row.path) - 게임이 제대로 돌지 않을 수 있습니다." }
        elseif ($row.PSObject.Properties['sha256'] -and $row.sha256 -and $h -ne $row.sha256) { Warn "원본 게임 파일이 일본판 1.20 과 다릅니다: $($row.path) - 그대로 설치합니다." }
    }
    if ($manifest.PSObject.Properties['check_packs'] -and $manifest.check_packs) {
        $packNames=@($manifest.files.path)+@($manifest.required.path)+@($manifest.known_packs)
        foreach ($file in @(Get-ChildItem -LiteralPath $GamePath -Filter 'dmpack*.dat' -File)) {
            if ($file.Name -notin $packNames) { Warn "이 패치가 모르는 전투 묶음이 있습니다: $($file.Name) - 다른 모드라면 전투가 달라질 수 있습니다." }
        }
    }
    $plan=New-Object 'System.Collections.Generic.List[object]'
    $dirs=New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($row in @($manifest.files)) {
        $dest=SafeJoin $GamePath $row.path; NoLinks $dest
        if ([IO.Directory]::Exists($dest)) { throw "파일 자리가 폴더입니다: $($row.path)" }
        if ($row.PSObject.Properties['preserve_existing'] -and $row.preserve_existing -and [IO.File]::Exists($dest)) {
            if ($row.PSObject.Properties['ini_merge'] -and $row.ini_merge) {
                $defaults=IniPairs (SafeJoin $payload $row.path)       # (변수에 받아야 목록이 하나씩 풀린다)
                $missing=@($defaults | Where-Object { -not (IniHas $dest $_.s $_.k) })
                if ($missing.Count) {
                    Write-Output "설정 파일 $($row.path): 기존 값은 그대로 두고 새 항목 $($missing.Count)개를 더합니다."
                    $plan.Add([pscustomobject]@{path=$row.path;before=(Hash $dest);after=$null;source='generated';config=$true;iniAdd=$missing})
                }
            }
            continue
        }
        $before=Hash $dest
        if ($before -eq $row.sha256) { continue }
        if ($before -and $row.PSObject.Properties['accepted_before'] -and $before -notin @($row.accepted_before)) { Warn "알려진 판과 다른 파일을 바꿉니다(백업해 둠): $($row.path)" }
        $isConfig=[bool](($row.PSObject.Properties['preserve_existing'] -and $row.preserve_existing) -or ($row.PSObject.Properties['game_writes'] -and $row.game_writes))   # 설정·게임이 고쳐 쓰는 기록
        $plan.Add([pscustomobject]@{path=$row.path;before=$before;after=$row.sha256;source='files';config=$isConfig})
        $dir=[IO.Path]::GetDirectoryName($dest)
        while ($dir -ne $GamePath -and -not [IO.Directory]::Exists($dir)) { $null=$dirs.Add($dir.Substring($GamePath.Length+1)); $dir=[IO.Path]::GetDirectoryName($dir) }
    }
    # 더는 쓰지 않는 파일: 알려진 판일 때만 백업으로 옮김
    foreach ($row in @($manifest.remove)) {
        if (-not $row) { continue }
        $dest=SafeJoin $GamePath $row.path; NoLinks $dest
        $before=Hash $dest
        if (-not $before) { continue }
        if ($before -in @($row.sha256)) { $plan.Add([pscustomobject]@{path=$row.path;before=$before;after=$null;source='remove'}) }
        else { Warn "$($row.path) 는(은) 이 패치가 모르는 판이라 그대로 둡니다. $($row.why)" }
    }
    $booksRow=$null
    if ($manifest.PSObject.Properties['books_add'] -and $manifest.books_add) {
        $iniPath=SafeJoin $GamePath 'ko-library/books.ini'
        if (-not [IO.File]::Exists($iniPath)) { throw '서재(ko-library\books.ini)가 없습니다. 본 패치를 먼저 설치하세요.' }
        $have=ReadBooks $iniPath
        $paths=@($have | ForEach-Object { if ($_.Contains('Path')) { $_['Path'].ToLowerInvariant() } })
        $new=New-Object 'System.Collections.Generic.List[object]'
        foreach ($b in @($manifest.books_add)) {
            if ($b.Path.ToLowerInvariant() -notin $paths) { $new.Add([ordered]@{Title=$b.Title;Path=$b.Path;Id=$b.Id}) }
        }
        if ($new.Count -gt 0) {
            foreach ($b in $have) { $new.Add($b) }
            $booksRow=[pscustomobject]@{path='ko-library/books.ini';before=(Hash $iniPath);after=$null;source='generated';text=(BooksText $new)}
            $plan.Add($booksRow)
        }
    }
    Write-Output ("확인 끝: 꾸러미 파일 {0}개, 바꿀 것 {1}개, 경고 {2}개." -f @($manifest.files).Count,$plan.Count,$Warnings.Count)
    if ($Mode -eq 'Check' -or $plan.Count -eq 0) { if ($plan.Count -eq 0) { Write-Output '이미 설치돼 있습니다.' }; exit 0 }
    if (-not $BackupPath) { $BackupPath=Join-Path ([IO.Path]::GetDirectoryName($GamePath)) ((Split-Path $GamePath -Leaf)+'.KHD-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)) }
    $BackupPath=Full $BackupPath; NoLinks $BackupPath
    if ((Within $BackupPath $GamePath) -or (Within $BackupPath $PackageRoot) -or (Within $GamePath $BackupPath) -or (Within $PackageRoot $BackupPath)) { throw '백업 자리가 게임·패치 폴더와 겹칩니다.' }
    if (Test-Path -LiteralPath $BackupPath) { throw '백업 폴더가 이미 있습니다. 새 폴더를 쓰세요.' }
    [IO.Directory]::CreateDirectory($BackupPath) | Out-Null
    foreach ($row in @($plan | Where-Object { $_.PSObject.Properties['iniAdd'] })) {
        $gen=SafeJoin (Join-Path $BackupPath 'generated') $row.path
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($gen)) | Out-Null
        [IO.File]::Copy((SafeJoin $GamePath $row.path),$gen,$false)
        foreach ($m in $row.iniAdd) { [void][KhdPatch.Ini]::WritePrivateProfileStringW($m.s,$m.k,$m.v,$gen) }
        $row.after=Hash $gen
        $row.PSObject.Properties.Remove('iniAdd')
    }
    if ($booksRow) {
        $gen=SafeJoin (Join-Path $BackupPath 'generated') $booksRow.path
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($gen)) | Out-Null
        [IO.File]::WriteAllText($gen,$booksRow.text,(New-Object System.Text.UnicodeEncoding($false,$true)))
        $booksRow.after=Hash $gen
        $booksRow.PSObject.Properties.Remove('text')
    }
    $state=[pscustomobject]@{format='khd-backup-v1';release=$manifest.release;target=$GamePath;tag=[guid]::NewGuid().ToString('N');status='backing_up';plan=@($plan.ToArray());created_directories=@($dirs);completed=@();warnings=@($Warnings)}
    WriteJson (Join-Path $BackupPath 'state.json') $state
    Write-Output '백업 중...'
    foreach ($row in $state.plan) {
        if ($row.before) {
            $src=SafeJoin $GamePath $row.path; $dest=SafeJoin (Join-Path $BackupPath 'original') $row.path
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest)) | Out-Null
            [IO.File]::Copy($src,$dest,$false)
            if ((Hash $dest) -ne $row.before -or (Hash $src) -ne $row.before) { throw "백업 확인 실패: $($row.path)" }
        }
    }
    $state.status='in_progress'; WriteJson (Join-Path $BackupPath 'state.json') $state
    Write-Output '설치 중...'
    try {
        GameStopped $GamePath
        foreach ($row in $state.plan) {
            $dest=SafeJoin $GamePath $row.path
            if ((Hash $dest) -ne $row.before) { throw "설치 중에 바뀐 파일이 있습니다: $($row.path)" }
            if ($row.source -eq 'remove') { [IO.File]::Delete($dest) }
            elseif ($row.source -eq 'generated') { PutFile (SafeJoin (Join-Path $BackupPath 'generated') $row.path) $dest $row.after $state.tag }
            else { PutFile (SafeJoin $payload $row.path) $dest $row.after $state.tag }
            $state.completed+=,$row.path
            WriteJson (Join-Path $BackupPath 'state.json') $state
        }
        $state.status='installed'; WriteJson (Join-Path $BackupPath 'state.json') $state
    } catch {
        $originalError=$_
        try { RestorePlan $state $BackupPath $GamePath; $state.status='rolled_back'; WriteJson (Join-Path $BackupPath 'state.json') $state }
        catch { $state.status='recovery_required'; WriteJson (Join-Path $BackupPath 'state.json') $state; Write-Warning "되돌리기를 다 못 했습니다. 백업을 지우지 마세요: $BackupPath" }
        throw $originalError
    }
    Write-Output "설치 끝. 백업: $BackupPath"
    Write-Output '되돌리려면 Restore.cmd 를 실행하고 위 백업 폴더를 넣으세요. 세이브와 설정은 그대로 두었습니다.'
    if ($Warnings.Count) { Write-Output "경고 $($Warnings.Count)개는 위에 있습니다." }
    exit 0
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
