param([string]$PythonPath='python',[string]$IsccPath='', [switch]$SkipBuild, [switch]$SkipTests, [string]$CompilerRoot='', [string]$QtRoot='', [string]$RuntimeDirectory='', [switch]$Offline)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$priorPythonUtf8=$env:PYTHONUTF8
Push-Location $root
try {
    $env:PYTHONUTF8='1'
    $branch=git branch --show-current
    if ($branch -and $branch -ne 'cpp-preview') { throw 'Preview packaging is restricted to the cpp-preview branch.' }
    $status=git status --porcelain
    if ($LASTEXITCODE -ne 0 -or $status) { throw 'Commit the complete release source before packaging.' }
    if (-not $IsccPath) { $compiler=Get-Command ISCC.exe -ErrorAction SilentlyContinue; $IsccPath=if ($compiler) { $compiler.Source } else { "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe" } }
    if (-not (Test-Path -LiteralPath $IsccPath)) { throw 'Provide Inno Setup 6 with -IsccPath.' }
    if (-not $SkipBuild) { & ./build.ps1 -PythonPath $PythonPath -CompilerRoot $CompilerRoot -QtRoot $QtRoot -RuntimeDirectory $RuntimeDirectory -Offline:$Offline | Out-Host }
    if (-not $SkipTests) { & ./tools/test.ps1 -SkipBuild | Out-Host }
    $version=(& $PythonPath tools/release.py version).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Invalid release version.' }
    $numericVersion=(& $PythonPath tools/release.py numeric-version).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Invalid numeric version.' }
    $portable=& $PythonPath tools/release.py stage
    if ($LASTEXITCODE -ne 0) { throw 'Portable staging failed.' }
    & $PythonPath tools/release.py smoke
    if ($LASTEXITCODE -ne 0) { throw 'Packaged C++ startup check failed.' }
    $output=Join-Path $root 'preview-release'
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    & $IsccPath '/Qp' "/DAppVersion=$version" "/DAppNumericVersion=$numericVersion" "/DAppDir=$portable" "/DOutputDir=$output" (Join-Path $root 'installer/LightBlockDesktop.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Setup compilation failed.' }
    & $PythonPath tools/release.py bundle
    if ($LASTEXITCODE -ne 0) { throw 'Release packaging failed.' }
    & $PythonPath tools/release.py verify
    if ($LASTEXITCODE -ne 0) { throw 'Release integrity verification failed.' }
    Get-ChildItem -LiteralPath $output -Filter "Qingkuai-Preview-v$version-*"
} finally { $env:PYTHONUTF8=$priorPythonUtf8;Pop-Location }
