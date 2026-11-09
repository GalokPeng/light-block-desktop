param(
    [string]$PythonPath = '',
    [string]$IsccPath = '',
    [switch]$SkipInstall
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root

$gitStatus = git status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'A Git checkout is required to archive the exact release source.' }
if ($gitStatus) { throw 'Commit or stash changes before packaging a release.' }
if (-not $IsccPath) {
    $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($compiler) { $IsccPath = $compiler.Source }
    else { $IsccPath = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
}
if (-not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) { throw 'Provide the Inno Setup compiler with -IsccPath.' }
$IsccPath = (Resolve-Path -LiteralPath $IsccPath).Path

& (Join-Path $root 'build.ps1') -PythonPath $PythonPath -SkipInstall:$SkipInstall
if (-not $PythonPath) { $PythonPath = Join-Path $root '.venv\Scripts\python.exe' }
$version = & $PythonPath tools/release.py version
if ($LASTEXITCODE -ne 0) { throw 'Could not read the app version.' }
$dist = Join-Path $root 'dist'
$portable = Join-Path $dist "Qingkuai-v$version-Windows-x64"
$output = Join-Path $root 'release'
New-Item -ItemType Directory -Force -Path $output | Out-Null

foreach ($file in @('LICENSE', 'README.md', 'CHANGES.md', 'THIRD_PARTY_NOTICES.md', 'TEST_REPORT.md')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $portable
}
& $PythonPath tools/release.py smoke
if ($LASTEXITCODE -ne 0) { throw 'Portable startup check failed.' }
& $IsccPath '/Qp' "/DAppVersion=$version" "/DAppDir=$portable" "/DOutputDir=$output" (Join-Path $root 'installer\LightBlockDesktop.iss')
if ($LASTEXITCODE -ne 0) { throw 'Setup compilation failed.' }
& $PythonPath tools/release.py bundle --dist $dist --output $output
if ($LASTEXITCODE -ne 0) { throw 'Release packaging failed.' }
Get-ChildItem -LiteralPath $output -Filter "Qingkuai-v$version-*"
