param([string]$PythonPath='python')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $root 'dist/LightBlockDesktop/轻块桌面.exe'
$qa=Join-Path $root ('.local/test-organizer-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $qa | Out-Null
function Run-Fixture([string[]]$Arguments) {
    $process=Start-Process -FilePath $exe -ArgumentList ($Arguments | ForEach-Object {'"'+$_+'"'}) -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(90000)){Stop-Process -Id $process.Id;throw 'Organizer fixture timed out'}
    if($process.ExitCode -ne 0){throw ('Organizer fixture failed: '+$process.ExitCode)}
}
Run-Fixture @('--organizer-test','--data-dir',(Join-Path $qa 'native-profile'),'--report',(Join-Path $qa 'unit.json'))
$unit=Get-Content (Join-Path $qa 'unit.json') -Raw | ConvertFrom-Json
if($unit.failed -ne 0){throw 'Organizer regression failed'}
$prior=$env:LIGHTBLOCK_TEST_SCOPE
try {
    $env:LIGHTBLOCK_TEST_SCOPE='organizer'
    Run-Fixture @('--ui-snapshots',(Join-Path $qa 'ui'),'--data-dir',(Join-Path $qa 'ui-profile'))
    $ui=Get-Content (Join-Path $qa 'ui/ui-checks.json') -Raw | ConvertFrom-Json
    if(-not $ui.passed){throw 'Organizer settings UI failed'}
}finally{$env:LIGHTBLOCK_TEST_SCOPE=$prior}
Write-Output ('PASS focused organizer regression; evidence: '+$qa)
