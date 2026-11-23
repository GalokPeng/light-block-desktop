param([switch]$SkipBuild, [ValidateSet('native-mf','qt-ffmpeg')][string]$VideoProbeBackend='native-mf', [string]$PythonPath='python')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $root 'build.ps1') }
$exe = Join-Path $root 'dist/LightBlockDesktop/轻块桌面.exe'
$qa = Join-Path $root ('.local/test-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $qa | Out-Null
function Invoke-NativeTest([string[]]$TestArgs, [int]$Timeout = 120000, [int]$ExpectedExit = 0) {
    $quoted = @($TestArgs | ForEach-Object { '"' + $_ + '"' })
    $testProcess = Start-Process -FilePath $exe -ArgumentList $quoted -WindowStyle Hidden -PassThru
    if (-not $testProcess.WaitForExit($Timeout)) {
        Stop-Process -Id $testProcess.Id -ErrorAction SilentlyContinue
        throw ('Native test timed out: ' + ($TestArgs -join ' '))
    }
    if ($testProcess.ExitCode -ne $ExpectedExit) { throw ('Native test failed with exit ' + $testProcess.ExitCode) }
}
$unit = Join-Path $qa 'unit.json'
Invoke-NativeTest @('--self-test','--data-dir',(Join-Path $qa 'unit-profile'),'--report',$unit) 180000
$unitResult = Get-Content -LiteralPath $unit -Raw | ConvertFrom-Json
if ($unitResult.failed -ne 0) { throw 'Native unit checks failed.' }
& $PythonPath (Join-Path $root 'tools/test_clipboard_io.py')
if ($LASTEXITCODE -ne 0) { throw 'Isolated clipboard regression failed.' }
$probes = @()
foreach ($name in @('night-drive.jpg','startrail.gif','motion.mp4')) {
    $report = Join-Path $qa ($name+'.json')
    $probeArgs = @('--worker','--path',(Join-Path $root ('assets/'+$name)),'--data-dir',(Join-Path $qa 'probe-profile'),'--report',$report)
    if ($name -like '*.mp4' -and $VideoProbeBackend -eq 'qt-ffmpeg') { $probeArgs += '--qt-media' }
    Invoke-NativeTest $probeArgs
    $result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    Write-Output ($name + ': ' + ($result | ConvertTo-Json -Compress))
    if (-not $result.ready -or $result.error -ne 0) { throw ('Media did not become ready: '+$name) }
    if ($name -like '*.gif' -and $result.image_changes -lt 3) { throw 'GIF did not animate.' }
    if ($name -like '*.mp4' -and $result.video_frames -lt 10) { throw 'Video did not render frames.' }
    if ($name -like '*.mp4' -and $VideoProbeBackend -eq 'qt-ffmpeg' -and $result.backend -ne 'qt-ffmpeg') { throw 'Requested fallback decoder was not exercised.' }
    $probes += [pscustomobject]@{file=$name;result=$result}
}
$stress = Join-Path $qa 'switching.json'
Invoke-NativeTest @('--stress-test','--data-dir',(Join-Path $qa 'stress-profile'),'--report',$stress) 120000
$stressResult = Get-Content -LiteralPath $stress -Raw | ConvertFrom-Json
if ($stressResult.failures -ne 0 -or $stressResult.confirmed_switches -ne 12 -or $stressResult.remaining_children -ne 0 -or $stressResult.max_children -ne 2 -or -not $stressResult.independent_channels -or $stressResult.thumbnails_rendered -lt 6 -or -not $stressResult.wallpaper_unchanged) { throw 'Switching / lifecycle regression.' }
$crash = Join-Path $qa 'crash.json'
Invoke-NativeTest @('--crash-test','--data-dir',(Join-Path $qa 'crash-profile'),'--report',$crash) 120000 99
$crashResult = Get-Content -LiteralPath $crash -Raw | ConvertFrom-Json
$deadline = [DateTime]::UtcNow.AddSeconds(5)
while ((Get-Process -Id $crashResult.child_pid -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
$childGone = -not (Get-Process -Id $crashResult.child_pid -ErrorAction SilentlyContinue)
if (-not $childGone) { throw 'Job object did not clean up after parent exit.' }
$uiDirectory = Join-Path $qa 'ui'
Invoke-NativeTest @('--ui-snapshots',$uiDirectory,'--data-dir',(Join-Path $qa 'ui-profile')) 180000
$uiResult = Get-Content -LiteralPath (Join-Path $uiDirectory 'ui-checks.json') -Raw | ConvertFrom-Json
$noteResult = Get-Content -LiteralPath (Join-Path $uiDirectory 'notes-ui-checks.json') -Raw | ConvertFrom-Json
if (-not $uiResult.passed -or -not $noteResult.passed) { throw 'C++ frontend or desktop note regression.' }
$assetHashes = @()
$origins = Get-Content -LiteralPath (Join-Path $root 'assets/wallpaper-origins.json') -Raw | ConvertFrom-Json
foreach ($wallpaper in $origins.wallpapers) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $root ('assets/'+$wallpaper.filename)) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $wallpaper.sha256) { throw ('Wallpaper bytes changed: '+$wallpaper.filename) }
    $assetHashes += [pscustomobject]@{file=$wallpaper.filename;sha256=$actual}
}
& $PythonPath (Join-Path $root 'tools/test_lifecycle.py') --exe $exe --output (Join-Path $qa 'lifecycle')
if ($LASTEXITCODE -ne 0) { throw 'Application exit / session / startup regression.' }
$lifecycleResult = Get-Content -LiteralPath (Join-Path $qa 'lifecycle/lifecycle.json') -Raw | ConvertFrom-Json
$summary = [ordered]@{
    created_at = [DateTime]::UtcNow.ToString('o')
    executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    executable_bytes = (Get-Item -LiteralPath $exe).Length
    native_unit = $unitResult
    actual_playback = $probes
    video_probe_backend = $VideoProbeBackend
    switching = $stressResult
    parent_exit_child_cleanup = $childGone
    lifecycle = $lifecycleResult
    cpp_ui = $uiResult
    desktop_note_ui = $noteResult
    ui_dll = (Get-Content -LiteralPath (Join-Path (Split-Path $exe) 'ui-current.txt') -Raw).Trim()
    original_jpeg_hashes = $assetHashes
    scope = 'Local-only; no install, no real desktop takeover, no original profile access.'
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $qa 'summary.json') -Encoding utf8
$summary | ConvertTo-Json -Depth 5
Write-Output ('Evidence: '+$qa)
