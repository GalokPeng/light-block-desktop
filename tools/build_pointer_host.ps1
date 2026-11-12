param([string]$ClangPath = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $ClangPath) {
    $ClangPath = Join-Path $root 'build/native-toolchain/Compiler/bin/clang++.exe'
    if (-not (Test-Path -LiteralPath $ClangPath)) { & (Join-Path $PSScriptRoot 'build_native_taskbar.ps1') | Out-Null }
}
$source = Join-Path $root 'native/pointer_host.cpp'
$target = Join-Path $root 'build/pointer-host'
New-Item -ItemType Directory -Force -Path $target | Out-Null
$hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.Substring(0,12).ToLowerInvariant()
$output = Join-Path $target "QingkuaiPointer-$hash.exe"
if (-not (Test-Path -LiteralPath $output)) {
    & $ClangPath --target=x86_64-w64-mingw32 -std=c++23 -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -municode -mwindows -static -Os -s `
        $source -o $output -lgdi32 -luser32
    if ($LASTEXITCODE -ne 0) { throw 'Could not build the cursor host.' }
}
Set-Content -LiteralPath (Join-Path $target 'current.txt') -Value ([IO.Path]::GetFileName($output)) -Encoding ascii
Write-Output $output
