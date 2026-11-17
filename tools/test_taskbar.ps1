param([string]$CompilerRoot='', [string]$ResourceCompiler='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $CompilerRoot) { $CompilerRoot=Join-Path (Split-Path $root) 'light-block-desktop/build/native-toolchain/Compiler' }
$clang=Join-Path $CompilerRoot 'bin/clang++.exe'
$output=Join-Path $root 'build/native-taskbar/layout-tests.exe'
New-Item -ItemType Directory -Force -Path (Split-Path $output) | Out-Null
if (-not $ResourceCompiler) { $ResourceCompiler=Join-Path $root 'build/qt-sdk/Tools/mingw1310_64/bin/windres.exe' }
if (-not (Test-Path -LiteralPath $ResourceCompiler)) { throw 'Provide the resource compiler from the build toolchain.' }
$manifestObject=Join-Path (Split-Path $output) 'layout-tests.manifest.o'
$originalPath=$env:PATH
Push-Location (Join-Path $root 'native/tests')
try {
    $env:PATH=(Split-Path $ResourceCompiler)+';'+$originalPath
    & $ResourceCompiler --codepage=65001 -i layout-tests.rc -O coff -o $manifestObject
    if ($LASTEXITCODE -ne 0) { throw 'Taskbar test manifest compilation failed.' }
} finally { $env:PATH=$originalPath;Pop-Location }
& $clang --target=x86_64-w64-mingw32 -std=c++23 -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -Wno-dll-attribute-on-redeclaration -static -O2 (Join-Path $root 'native/tests/taskbar_layout_tests.cpp') $manifestObject -o $output -ladvapi32 -lole32 -loleaut32 -lruntimeobject -luuid -lshell32
if ($LASTEXITCODE -ne 0) { throw 'Isolated taskbar test compilation failed.' }
# Keep the adjacent copy consistent for older developer tooling; the executable
# also embeds it, independent of the machine's external-manifest preference.
Copy-Item -LiteralPath (Join-Path $root 'native/tests/layout-tests.exe.manifest') -Destination "$output.manifest" -Force
& $output
if ($LASTEXITCODE -ne 0) { throw 'Isolated taskbar tests failed.' }
