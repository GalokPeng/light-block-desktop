param([string]$CompilerRoot='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $CompilerRoot) { $CompilerRoot=& (Join-Path $PSScriptRoot 'setup_native_toolchain.ps1') }
$clang=Join-Path $CompilerRoot 'bin/clang++.exe'
$source=Join-Path $root 'native/taskbar_layout.cpp'
$output=Join-Path $root 'build/native-taskbar'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$hash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.Substring(0,12).ToLowerInvariant()
$dll=Join-Path $output ('LightBlockNativeTaskbar-'+$hash+'.dll')
if (-not (Test-Path -LiteralPath $dll)) {
    & $clang --target=x86_64-w64-mingw32 -std=c++23 -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 ('-DLB_NATIVE_LAYOUT_BUILD=0x'+$hash+'ULL') -Wno-dll-attribute-on-redeclaration -shared -static -O2 $source -o $dll -ladvapi32 -lole32 -loleaut32 -lruntimeobject -luuid -lshell32
    if ($LASTEXITCODE -ne 0) { throw 'Native C++ taskbar component build failed.' }
}
Set-Content -LiteralPath (Join-Path $output 'current.txt') -Value ([IO.Path]::GetFileName($dll)) -Encoding ascii
Write-Output $dll
