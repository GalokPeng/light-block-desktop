param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $root 'build.ps1') }
$exe=Join-Path $root 'dist/LightBlockDesktop/轻块桌面.exe'
$qa=Join-Path $root ('.local/performance-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $qa | Out-Null
$report=Join-Path $qa 'samples.json'
$profile=Join-Path $qa 'profile'
$arguments=@('--performance-test',$report,'--data-dir',$profile) | ForEach-Object { '"'+$_+'"' }
$probe=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (-not $probe.WaitForExit(125000)) {
    Stop-Process -Id $probe.Id -ErrorAction SilentlyContinue
    throw 'Performance fixture timed out; only its isolated process was stopped.'
}
if ($probe.ExitCode -ne 0) { throw "Performance fixture exited with $($probe.ExitCode)" }
$result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
if ($result.failures -ne 0 -or $result.ready_switches -ne 12 -or $result.remaining_children -ne 0 -or $result.max_children -gt 1) { throw 'Performance lifecycle check failed; inspect samples.json.' }
$initial=$result.samples | Where-Object phase -eq -1 | Select-Object -Last 1
$final=$result.samples | Where-Object { $_.phase -eq 12 -and $_.children -eq 0 } | Select-Object -Last 1
if (-not $initial -or -not $final -or $final.parent_private-$initial.parent_private -gt 16MB -or $final.gdi-$initial.gdi -gt 8 -or $final.user-$initial.user -gt 8) { throw 'Repeated media switching retained unexpected resources; inspect samples.json.' }
$phases=@($result.samples | Group-Object phase | ForEach-Object {
    $group=$_.Group
    [ordered]@{
        phase=[int]$_.Name
        parent_median_mib=[math]::Round(($group.parent_private | Sort-Object)[[int]($group.Count/2)]/1MB,2)
        total_peak_private_mib=[math]::Round(($group.total_private | Measure-Object -Maximum).Maximum/1MB,2)
        total_peak_working_set_mib=[math]::Round(($group.total_working_set | Measure-Object -Maximum).Maximum/1MB,2)
        peak_gdi=($group.gdi | Measure-Object -Maximum).Maximum
        peak_user=($group.user | Measure-Object -Maximum).Maximum
    }
})
$summary=[ordered]@{
    passed=$true
    created_at=[DateTime]::UtcNow.ToString('o')
    executable_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    ui_dll=(Get-Content -LiteralPath (Join-Path (Split-Path $exe) 'ui-current.txt') -Raw).Trim()
    scope=$result.scope
    phase_key='-1 idle tray; 0..11 repeat image/GIF/video four times; 12 all media stopped. Native MF only; not a GPU/render-quality benchmark.'
    elapsed_seconds=[math]::Round($result.elapsed_ms/1000,2)
    sampled_cpu_seconds=[math]::Round($result.sampled_cpu_seconds,3)
    average_cpu_one_core_percent=[math]::Round(100000*$result.sampled_cpu_seconds/$result.elapsed_ms,2)
    peak_total_private_mib=[math]::Round($result.peak_total_private_bytes/1MB,2)
    initial_idle_private_mib=[math]::Round($initial.parent_private/1MB,2)
    final_idle_private_mib=[math]::Round($final.parent_private/1MB,2)
    final_idle_gdi=$final.gdi
    final_idle_user=$final.user
    phases=$phases
    caution='Working-set totals double-count shared pages. Private bytes are committed process memory, not VRAM. No forced working-set trimming. Child CPU time is sampled and omits the final interval before exit. Hidden video decoding does not prove smooth visible desktop composition.'
}
$summary | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $qa 'summary.json') -Encoding utf8
$summary | ConvertTo-Json -Depth 7
Write-Output ('Evidence: '+$qa)
