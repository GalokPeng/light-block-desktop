param([string]$ClangPath = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $ClangPath) {
    $compilerRoot = & (Join-Path $PSScriptRoot 'setup_native_toolchain.ps1')
    $ClangPath = Join-Path $compilerRoot 'bin/clang++.exe'
}
$source = Join-Path $root 'native/update_launcher.cpp'
$target = Join-Path $root 'build/update-launcher'
New-Item -ItemType Directory -Force -Path $target | Out-Null
$identity = Join-Path $root 'src/version.hpp'
$digest = [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash+(Get-FileHash -LiteralPath $identity -Algorithm SHA256).Hash))
$hash = [Convert]::ToHexString($digest).Substring(0,12).ToLowerInvariant()
$output = Join-Path $target "QingkuaiUpdate-$hash.exe"
if (-not (Test-Path -LiteralPath $output)) {
    & $ClangPath --target=x86_64-w64-mingw32 -std=c++23 -DUNICODE -D_UNICODE -municode -static -Os -s `
        $source -o $output -lbcrypt -lshell32
    if ($LASTEXITCODE -ne 0) { throw 'Could not build the update launcher.' }
}
Set-Content -LiteralPath (Join-Path $target 'current.txt') -Value ([IO.Path]::GetFileName($output)) -Encoding ascii
Write-Output $output
