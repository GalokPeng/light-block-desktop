# Read-only snapshot. Running a second time with -Baseline compares the same item.
[CmdletBinding()]
param(
    [string[]]$ItemName = @('微信', 'WeChat', 'Weixin'),
    [string]$Baseline,
    [string]$OutputPath,
    [string]$ProfileDirectory,
    [string]$UserDesktop = [Environment]::GetFolderPath('DesktopDirectory'),
    [string]$PublicDesktop = [Environment]::GetFolderPath('CommonDesktopDirectory')
)
$ErrorActionPreference = 'Stop'
$names = @($ItemName | ForEach-Object { $_.Trim() } | Where-Object { $_ } | Sort-Object -Unique)
if (-not $names.Count) { throw 'Specify the desktop display name using -ItemName.' }
$previous = $null
if ($Baseline) {
    $previous = Get-Content -LiteralPath $Baseline -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($previous.schema -ne 2 -or -not $previous.salt) { throw 'Unsupported baseline report.' }
}
$salt = if ($previous) { $previous.salt } else { [guid]::NewGuid().ToString('N') }
function Fingerprint([string]$value) {
    if ([string]::IsNullOrEmpty($value)) { return $null }
    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($salt + [char]0 + $value)
        return [BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-', '').Substring(0,32)
    } finally { $hash.Dispose() }
}
$query = Fingerprint (($names | ForEach-Object { $_.ToUpperInvariant() }) -join [char]0)
if ($previous -and $query -ne $previous.query) { throw 'Use the same -ItemName value as the baseline.' }
function PathKey([string]$value) { return Fingerprint ($value.ToUpperInvariant()) }
function Is-Match([IO.FileInfo]$file) { return ($file.BaseName -in $names -or $file.Name -in $names) }
$shell = New-Object -ComObject WScript.Shell
$roots = @()
$links = @(
    foreach ($location in 'user','public') {
        $root = if ($location -eq 'user') { $UserDesktop } else { $PublicDesktop }
        try {
            if (-not $root) { throw 'unavailable' }
            $matchedFiles = @(Get-ChildItem -LiteralPath $root -Force -File | Where-Object { Is-Match $_ })
            $roots += [pscustomobject]@{ location=$location; readable=$true; matches=$matchedFiles.Count }
        } catch {
            $roots += [pscustomobject]@{ location=$location; readable=$false }
            continue
        }
        foreach ($file in $matchedFiles) {
            $extension = $file.Extension.ToLowerInvariant()
            $type = if ($extension -in '.lnk','.url','.website','.appref-ms') { $extension } else { 'other-file' }
            $item = [ordered]@{ id="$location/$(PathKey $file.Name)"; nameKey=(PathKey $file.BaseName); location=$location; pathKey=(PathKey $file.FullName); type=$type; status='metadata-only' }
            # Do not hydrate cloud placeholders, traverse reparse points, or hash arbitrary desktop files.
            if ($type -ne 'other-file' -and -not ([int]$file.Attributes -band 0x441400) -and $file.Length -le 4MB) {
                try {
                    $before = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
                    $item.content = Fingerprint $before
                    if ($type -eq '.lnk') {
                        $lnk = $shell.CreateShortcut($file.FullName)
                        try {
                            $dest = [string]$lnk.TargetPath
                            $item.target = PathKey $dest
                            $item.arguments = Fingerprint ([string]$lnk.Arguments)
                            $item.icon = PathKey ([string]$lnk.IconLocation)
                            $item.workingDirectory = PathKey ([string]$lnk.WorkingDirectory)
                            $item.targetKind = if (-not $dest) { 'shell-or-unresolved' } elseif ($dest -match '(?i)(^|[\\/])explorer\.exe$') { 'explorer' } elseif ($dest -like '*.exe') { 'executable' } else { 'other' }
                        } finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($lnk) }
                    } elseif ($type -in '.url','.website') {
                        $section = ''
                        foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
                            if ($line -match '^\s*\[([^\]]+)\]') { $section = $Matches[1] }
                            elseif ($section -eq 'InternetShortcut' -and $line -match '^\s*URL\s*=(.*)$') { $item.target = Fingerprint $Matches[1].Trim(); break }
                        }
                    }
                    $after = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
                    $item.status = if ($before -eq $after) { 'read' } else { 'changed-during-read' }
                } catch { $item.status = 'unreadable' }
            }
            [pscustomobject]$item
        }
    }
)
[void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
$run = Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -ErrorAction SilentlyContinue
$command = [string]$run.StillWallpaper
$registered = [regex]::Match($command,'^"([^"]+)"\s+--background(?:\s|$)')
$installed = $registered.Success -and (Test-Path -LiteralPath $registered.Groups[1].Value -PathType Leaf)
$profiles = @()
if ($ProfileDirectory) { $profiles = @($ProfileDirectory) }
else {
    $custom = [regex]::Match($command,'\s--data-dir\s+"([^"]+)"')
    if ($custom.Success) { $profiles = @($custom.Groups[1].Value) }
    else {
        if ($installed) { $profiles += Join-Path (Split-Path $registered.Groups[1].Value) 'data/profile-v2' }
        $uninstall = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{3CD16B31-077E-4CE9-BB01-578642D94876}_is1'
        foreach ($hive in 'HKCU:', 'HKLM:') {
            $record = Get-ItemProperty -LiteralPath (Join-Path $hive $uninstall) -ErrorAction SilentlyContinue
            if ($record.InstallLocation) { $profiles += Join-Path $record.InstallLocation 'data/profile-v2' }
        }
        $profiles += Join-Path $env:LOCALAPPDATA 'StillWallpaper/profile-v2'
    }
}
$states = @(foreach ($directory in $profiles | Select-Object -Unique) {
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) { continue }
    $json = Join-Path $directory 'organizer.json'
    $state = [ordered]@{ profileKey=(PathKey $directory); organizerFile=(Test-Path -LiteralPath $json); visibilityReceipt=(Test-Path -LiteralPath (Join-Path $directory 'native-desktop-icons.json')) }
    if ($state.organizerFile) {
        try {
            $model = Get-Content -LiteralPath $json -Raw -Encoding UTF8 | ConvertFrom-Json
            $state.thisPcEntries = @($model.entries | Where-Object path -eq 'shell:MyComputerFolder').Count
            $state.matches = @(foreach ($entry in $model.entries) {
                $path = [string]$entry.path
                if ($entry.name -notin $names -and [IO.Path]::GetFileNameWithoutExtension($path) -notin $names) { continue }
                $pk = PathKey $path
                [pscustomobject]@{ labelKey=(PathKey ([string]$entry.name)); pathKey=$pk; shellItem=$path.StartsWith('shell:', [StringComparison]::OrdinalIgnoreCase); matchesDesktopPath=($pk -in $links.pathKey) }
            })
        } catch { $state.modelReadError = $true }
    }
    [pscustomobject]$state
})
$changes = @(if ($previous) {
    $ids = @($previous.shortcuts | ForEach-Object { $_.id }) + @($links | ForEach-Object { $_.id })
    foreach ($id in $ids | Sort-Object -Unique) {
        $old = $previous.shortcuts | Where-Object id -eq $id
        $now = $links | Where-Object id -eq $id
        $fields = if (-not $old) { 'appeared' } elseif (-not $now) { 'absent-from-scan' } else {
            foreach ($field in 'type','status','targetKind','target','arguments','workingDirectory','icon','content') {
                if ($old.$field -cne $now.$field) { $field }
            }
        }
        if ($fields) { [pscustomobject]@{ id=$id; changes=@($fields) } }
    }
})
$iconPrefs = Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\NewStartPanel' -ErrorAction SilentlyContinue
$linkClass = Get-Item -LiteralPath 'Registry::HKEY_CLASSES_ROOT\.lnk' -ErrorAction SilentlyContinue
$choice = Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.lnk\UserChoice' -ErrorAction SilentlyContinue
$report = [ordered]@{
    shellLinkClassIsStandard=$(if($linkClass) { $linkClass.GetValue('') -eq 'lnkfile' } else { $null })
    shellLinkUserChoicePresent=($null -ne $choice); shellLinkUserChoiceKey=(PathKey ([string]$choice.ProgId))
    schema=2; capturedUtc=[DateTime]::UtcNow.ToString('o'); salt=$salt; query=$query; osBuild=[Environment]::OSVersion.Version.Build
    startupCommandValid=$installed; startupFileVersion=$(if ($installed) { (Get-Item -LiteralPath $registered.Groups[1].Value).VersionInfo.FileVersion })
    thisPcUserHideFlag=$iconPrefs.'{20D04FE0-3AEA-1069-A2D8-08002B30309D}'
    desktopFolders=$roots; shortcuts=$links; duplicateNameAcrossRoots=@($links | Group-Object nameKey | Where-Object { @($_.Group.location | Sort-Object -Unique).Count -gt 1 }).Count
    candidateProfiles=$states; comparedTo=$(if ($previous) { $previous.capturedUtc }); shortcutDifferences=$changes
}
$result = $report | ConvertTo-Json -Depth 7
if ($OutputPath) {
    $path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
    if ([IO.Path]::GetExtension($path) -ne '.json') { throw 'OutputPath must be a new .json file.' }
    $stream = [IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $bytes = [Text.UTF8Encoding]::new($true).GetPreamble() + [Text.Encoding]::UTF8.GetBytes($result); $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
} else { $result }
