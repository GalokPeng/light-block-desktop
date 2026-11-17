param([string]$ClangPath = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build_native_taskbar.ps1') -ClangPath $ClangPath | Out-Host
if (-not $ClangPath) { $ClangPath = Join-Path $root 'build/native-toolchain/Compiler/bin/clang++.exe' }
& (Join-Path $PSScriptRoot 'test_taskbar.ps1') -CompilerRoot (Split-Path (Split-Path $ClangPath))
