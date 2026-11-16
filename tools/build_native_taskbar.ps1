# Compatibility entry point for developer scripts; the product uses the same builder.
param([string]$ClangPath='')
$ErrorActionPreference='Stop'
$compilerRoot=if ($ClangPath) { Split-Path (Split-Path $ClangPath) } else { & (Join-Path $PSScriptRoot 'setup_native_toolchain.ps1') }
& (Join-Path $PSScriptRoot 'build_taskbar.ps1') -CompilerRoot $compilerRoot
