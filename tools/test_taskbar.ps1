param([string]$CompilerRoot='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $CompilerRoot) { $CompilerRoot=Join-Path (Split-Path $root) 'light-block-desktop/build/native-toolchain/Compiler' }
$clang=Join-Path $CompilerRoot 'bin/clang++.exe'
$output=Join-Path $root 'build/native-taskbar/layout-tests.exe'
New-Item -ItemType Directory -Force -Path (Split-Path $output) | Out-Null
& $clang --target=x86_64-w64-mingw32 -std=c++23 -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -Wno-dll-attribute-on-redeclaration -static -O2 (Join-Path $root 'native/tests/taskbar_layout_tests.cpp') -o $output -ladvapi32 -lole32 -loleaut32 -lruntimeobject -luuid -lshell32
if ($LASTEXITCODE -ne 0) { throw 'Isolated taskbar test compilation failed.' }
Copy-Item -LiteralPath (Join-Path $root 'native/tests/layout-tests.exe.manifest') -Destination "$output.manifest" -Force
& $output
if ($LASTEXITCODE -ne 0) { throw 'Isolated taskbar tests failed.' }
