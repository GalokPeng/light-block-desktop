param(
    [string]$FFmpegPath = '',
    [string]$PythonPath = '',
    [string]$DistPath = '',
    [string]$WorkPath = '',
    [switch]$SkipInstall
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
if (-not $PythonPath) {
    $PythonPath = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $PythonPath)) {
        py -3.11 -m venv .venv
        if ($LASTEXITCODE -ne 0) { throw 'Python 3.11 is required.' }
    }
}
$PythonPath = (Get-Command $PythonPath -ErrorAction Stop).Source
if (-not $DistPath) { $DistPath = Join-Path $PSScriptRoot 'dist' }
if (-not $WorkPath) { $WorkPath = Join-Path $PSScriptRoot 'build' }
$version = & $PythonPath tools/release.py version
if ($LASTEXITCODE -ne 0) { throw 'Could not read the app version.' }
$portable = Join-Path $DistPath "Qingkuai-v$version-Windows-x64"
if (Test-Path -LiteralPath (Join-Path $portable 'data')) {
    throw 'The build output contains a user data folder. Choose a different -DistPath.'
}
if (-not $SkipInstall) {
    & $PythonPath -m pip install -r requirements.txt -r requirements-build.txt
    if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
}
$previousPath = $env:PATH
$previousFFmpeg = $env:STILL_FFMPEG
$previousTaskbar = $env:STILL_TASKBAR_DLL
$previousCache = $env:PYINSTALLER_CONFIG_DIR
try {
    if (-not $FFmpegPath) {
        $FFmpegPath = & $PythonPath tools/build_recording_runtime.py
        if ($LASTEXITCODE -ne 0) { throw 'Could not build the recording runtime.' }
    }
    $env:STILL_FFMPEG = (Resolve-Path -LiteralPath $FFmpegPath).Path
    $env:STILL_TASKBAR_DLL = & (Join-Path $PSScriptRoot 'tools/build_native_taskbar.ps1')
    # Collect dependencies from Python, Qt and Windows only.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:PYINSTALLER_CONFIG_DIR = Join-Path $WorkPath 'pyinstaller-cache'
    & $PythonPath -m PyInstaller --clean --noconfirm --distpath $DistPath --workpath $WorkPath Still.spec
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
} finally {
    $env:PATH = $previousPath
    $env:STILL_FFMPEG = $previousFFmpeg
    $env:STILL_TASKBAR_DLL = $previousTaskbar
    $env:PYINSTALLER_CONFIG_DIR = $previousCache
}
Write-Host "Built: $portable"
