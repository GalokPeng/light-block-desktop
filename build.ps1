param([string]$CompilerRoot = '', [string]$ResourceCompiler = '', [string]$QtRoot = '', [string]$RuntimeDirectory='', [string]$PythonPath='python', [switch]$SkipUi, [switch]$Offline)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$CompilerRoot = & (Join-Path $root 'tools/setup_native_toolchain.ps1') -CompilerRoot $CompilerRoot -Offline:$Offline
$clang = Join-Path $CompilerRoot 'bin/clang++.exe'
if (-not (Test-Path -LiteralPath $clang)) { throw 'Pass -CompilerRoot pointing to an LLVM-MinGW toolchain containing bin/clang++.exe.' }
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist/LightBlockDesktop'
New-Item -ItemType Directory -Force -Path $build, $dist | Out-Null
$buildGuard=$null
try {
$buildGuard=[IO.File]::Open((Join-Path $build 'native-build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
if (-not $SkipUi) { & (Join-Path $root 'tools/build_ui.ps1') -QtRoot $QtRoot -DestinationDirectory $dist -Offline:$Offline | Out-Null }
if (-not $QtRoot) { $QtRoot=Join-Path $root 'build/qt-sdk' }
if (-not $ResourceCompiler) { $ResourceCompiler=Join-Path $QtRoot 'Tools/mingw1310_64/bin/windres.exe' }
if (-not (Test-Path -LiteralPath $ResourceCompiler)) { throw 'Pass -ResourceCompiler pointing to MinGW windres.exe, or prepare the Qt SDK.' }
& (Join-Path $root 'tools/stage_recording.ps1') -RuntimeDirectory $RuntimeDirectory -DestinationDirectory (Join-Path $dist 'runtime/recording') -PythonPath $PythonPath -Offline:$Offline | Out-Null
Push-Location (Join-Path $root 'resources')
$resourcePath=$env:PATH
try { $env:PATH=(Split-Path $ResourceCompiler)+';'+$resourcePath;& $ResourceCompiler --codepage=65001 -i app.rc -O coff -o (Join-Path $build 'app.res.o'); if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' } }
finally { $env:PATH=$resourcePath;Pop-Location }
$sources = @(Get-ChildItem -LiteralPath (Join-Path $root 'src') -Filter '*.cpp')
$exe = Join-Path $dist '轻块桌面.exe'
$linkedExe = Join-Path $build 'LightBlockDesktopNative.next.exe'
$flags = @('--target=x86_64-w64-mingw32','-DUNICODE','-D_UNICODE','-DWINVER=0x0A00','-D_WIN32_WINNT=0x0A00','-std=c++23','-Os','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',('-I'+$build))
$pch = Join-Path $build 'native.pch'
$pchHeader = Join-Path $root 'src/native.hpp'
$signature = ((Get-FileHash -LiteralPath $pchHeader).Hash + ($flags -join ' ') + (Get-Item -LiteralPath $clang).LastWriteTimeUtc.Ticks)
$signatureFile = Join-Path $build 'compiler-signature.txt'
$rebuildPch = -not (Test-Path -LiteralPath $pch) -or -not (Test-Path -LiteralPath $signatureFile) -or (Get-Content -LiteralPath $signatureFile -Raw).Trim() -ne $signature
if ($rebuildPch) {
    & $clang @flags -x c++-header $pchHeader -o $pch
    if ($LASTEXITCODE -ne 0) { throw 'C++ precompiled header failed.' }
    Set-Content -LiteralPath $signatureFile -Value $signature -Encoding utf8
}
$headers = @(Get-ChildItem -LiteralPath (Join-Path $root 'src') -File | Where-Object Extension -In '.h','.hpp')
$headerStamp = ($headers | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
$runtimeStamp = (Get-Item -LiteralPath (Join-Path $build 'runtime_identity.hpp')).LastWriteTimeUtc
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $build ($source.BaseName + '.o')
    if ($rebuildPch -or -not (Test-Path -LiteralPath $object) -or (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt $source.LastWriteTimeUtc -or (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt $headerStamp -or ($source.BaseName -eq 'media_process' -and (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt $runtimeStamp)) {
        & $clang @flags -include-pch $pch -c $source.FullName -o $object
        if ($LASTEXITCODE -ne 0) { throw ('C++ compilation failed: ' + $source.Name) }
    }
    $objects += $object
}
& $clang --target=x86_64-w64-mingw32 -municode -mwindows -static $objects (Join-Path $build 'app.res.o') -o $linkedExe -s -luser32 -lgdi32 -lshell32 -lcomctl32 -lshlwapi -ldwmapi -lole32 -loleaut32 -lwindowscodecs -luuid -lmfuuid -lpropsys -lmsimg32 -lcomdlg32 -lpsapi -ladvapi32 -lwinhttp -lbcrypt -lruntimeobject -lgdiplus
if ($LASTEXITCODE -ne 0) { throw 'C++ link failed.' }
# Indexers/antivirus may briefly open a newly linked image. Never terminate a
# running preview to replace it; retain the complete next image on failure.
for ($attempt=0; $attempt -lt 10; $attempt++) {
    try {
        if (Test-Path -LiteralPath $exe) { [IO.File]::Replace($linkedExe,$exe,[NullString]::Value) } else { [IO.File]::Move($linkedExe,$exe) }
        break
    } catch [IO.IOException] {
        if ($attempt -eq 9) { throw }
        Start-Sleep -Milliseconds 200
    }
}
New-Item -ItemType Directory -Force -Path (Join-Path $dist 'assets') | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'assets') -File | Copy-Item -Destination (Join-Path $dist 'assets') -Force
$cursorSource = Join-Path $root 'assets/cursors'
$cursorDestination = Join-Path $dist 'assets/cursors'
New-Item -ItemType Directory -Force -Path $cursorDestination | Out-Null
$cursorManifest = Join-Path $cursorSource 'manifest.json'
Copy-Item -LiteralPath $cursorManifest -Destination $cursorDestination -Force
foreach ($theme in (Get-Content -Raw -LiteralPath $cursorManifest | ConvertFrom-Json).themes) {
    Copy-Item -LiteralPath (Join-Path $cursorSource $theme) -Destination $cursorDestination -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $root 'README.md'),(Join-Path $root 'LICENSE') -Destination $dist -Force
$runtime = Join-Path $dist 'runtime'
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
$layout = & (Join-Path $root 'tools/build_taskbar.ps1') -CompilerRoot $CompilerRoot
Copy-Item -LiteralPath $layout -Destination $runtime -Force
Set-Content -LiteralPath (Join-Path $runtime 'taskbar-current.txt') -Value ([IO.Path]::GetFileName($layout)) -Encoding ascii
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $dist -Recurse -Force
$updater = & (Join-Path $root 'tools/build_update_launcher.ps1') -ClangPath $clang
Copy-Item -LiteralPath $updater -Destination (Join-Path $runtime 'QingkuaiUpdate.exe') -Force

Write-Output $exe
}
finally { if ($buildGuard) { $buildGuard.Dispose() } }
