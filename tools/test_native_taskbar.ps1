param([string]$ClangPath = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build_native_taskbar.ps1') -ClangPath $ClangPath | Out-Host
if (-not $ClangPath) { $ClangPath = Join-Path $root 'build/native-toolchain/Compiler/bin/clang++.exe' }
$source = Join-Path $root 'native/tests/taskbar_layout_tests.cpp'
$output = Join-Path $root 'build/native-taskbar/layout-tests.exe'
& $ClangPath --target=x86_64-w64-mingw32 -std=c++23 -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 `
    -Wno-dll-attribute-on-redeclaration -static -O2 $source -o $output -ladvapi32 -lole32 -loleaut32 -lruntimeobject -luuid
if ($LASTEXITCODE -ne 0) { throw 'Could not build the isolated taskbar tests.' }
Copy-Item -LiteralPath (Join-Path $root 'native/tests/layout-tests.exe.manifest') -Destination "$output.manifest" -Force
& $output
if ($LASTEXITCODE -ne 0) { throw 'The isolated taskbar tests failed.' }
