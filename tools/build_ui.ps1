param([string]$QtRoot = '', [string]$Compiler = '', [string]$DestinationDirectory = '', [switch]$Offline)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force -Path (Join-Path $root 'build') | Out-Null
$guard = [IO.File]::Open((Join-Path $root 'build/qt-ui-build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$oldPath = $env:PATH
try {
    $sdk = & (Join-Path $PSScriptRoot 'setup_ui_sdk.ps1') -QtRoot $QtRoot -Offline:$Offline
    $QtRoot = $sdk.QtRoot
    if (-not $Compiler) { $Compiler = $sdk.Compiler }
    if (-not $DestinationDirectory) { $DestinationDirectory = Join-Path $root 'dist/LightBlockDesktop' }
    $dist = [IO.Path]::GetFullPath($DestinationDirectory)
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Qt frontend requires GCC 13.1 from the pinned MinGW SDK.' }
    $env:PATH = (Split-Path $Compiler)+';'+$oldPath
    $compilerVersion = (& $Compiler -dumpfullversion).Trim()
    if ($LASTEXITCODE -ne 0 -or $compilerVersion -ne '13.1.0') { throw 'Qt 6.8.3 frontend requires its matching GCC 13.1.0 ABI.' }
    $dll = Join-Path $root 'build/LightBlockUi.next.dll'
    $includes = @('include','include/QtWidgets','include/QtGui','include/QtCore','include/QtConcurrent','include/QtNetwork') | ForEach-Object { '-I'+(Join-Path $QtRoot $_) }
    $inputs = @(Get-ChildItem -LiteralPath (Join-Path $root 'ui') -Recurse -File) + @(Get-ChildItem -LiteralPath (Join-Path $root 'src') -Filter '*ui_api.h') + @(Get-Item -LiteralPath (Join-Path $root 'src/version.hpp'),(Join-Path $root 'src/window_shape.hpp'),(Join-Path $root 'src/desktop_layer.hpp'),$Compiler,(Join-Path $QtRoot 'bin/Qt6Core.dll'))
    $signature = (@($inputs | Sort-Object FullName | ForEach-Object { $_.FullName+':'+(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }) -join "`n") + "`nflags-v2-c++23-O2-shared-network"
    $stamp = Join-Path $root 'build/qt-ui-signature.txt'
    if (-not (Test-Path -LiteralPath $dll) -or -not (Test-Path -LiteralPath $stamp) -or (Get-Content -LiteralPath $stamp -Raw) -cne $signature) {
        & $Compiler -std=c++23 -O2 -shared -Wall -Wextra -Werror -Wno-misleading-indentation -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 @includes (Join-Path $root 'ui/frontend.cpp') (Join-Path $root 'ui/notes.cpp') (Join-Path $root 'ui/capture_controls.cpp') (Join-Path $root 'ui/recording_controls.cpp') (Join-Path $root 'ui/scroll_controls.cpp') ('-L'+(Join-Path $QtRoot 'lib')) -o $dll -lQt6Widgets -lQt6Gui -lQt6Core -lQt6Concurrent -lQt6Network -ldwmapi -lshell32 -luser32 -lgdi32 -lole32
        if ($LASTEXITCODE -ne 0) { throw 'C++ Qt frontend compilation failed.' }
        [IO.File]::WriteAllText($stamp,$signature)
    }
    $uiHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
    $uiName = 'LightBlockUi-'+$uiHash.Substring(0,12)+'.dll'
    $files = [Collections.Generic.List[object]]::new()
    function Stage-UiFile([string]$Source,[string]$Relative) {
        $destination = Join-Path $dist $Relative
        New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
        $hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash.ToLowerInvariant()
        if (-not (Test-Path -LiteralPath $destination) -or (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) { Copy-Item -LiteralPath $Source -Destination $destination -Force }
        $files.Add(@{path=$Relative.Replace('\','/');sha256=$hash})
    }
    Stage-UiFile $dll $uiName
    $mediaDll=& (Join-Path $PSScriptRoot 'build_media_ui.ps1') -QtRoot $QtRoot -Compiler $Compiler -Offline:$Offline
    $mediaName='LightBlockMedia-'+(Get-FileHash -LiteralPath $mediaDll).Hash.Substring(0,12).ToLowerInvariant()+'.dll'
    Stage-UiFile $mediaDll $mediaName
    $mediaPointer=Join-Path $root 'build/media-current.txt'
    Set-Content -LiteralPath $mediaPointer -Value $mediaName -Encoding ascii
    Stage-UiFile $mediaPointer 'runtime/media-current.txt'
    foreach ($name in @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Concurrent.dll','Qt6Network.dll','Qt6Multimedia.dll','Qt6MultimediaWidgets.dll','Qt6OpenGL.dll','Qt6OpenGLWidgets.dll','avcodec-61.dll','avformat-61.dll','avutil-59.dll','swresample-5.dll','swscale-8.dll')) { Stage-UiFile (Join-Path $QtRoot ('bin/'+$name)) $name }
    foreach ($name in @('libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll')) { Stage-UiFile (Join-Path (Split-Path $Compiler) $name) $name }
    foreach ($folder in @('platforms','imageformats','styles','tls','multimedia')) {
        foreach ($plugin in (Get-ChildItem -LiteralPath (Join-Path $QtRoot ('plugins/'+$folder)) -Filter '*.dll' -File)) { Stage-UiFile $plugin.FullName ('qt-plugins/'+$folder+'/'+$plugin.Name) }
    }
    foreach ($name in @('dark.qss','light.qss')) { Stage-UiFile (Join-Path $root ('ui/'+$name)) ('ui/'+$name) }
    Stage-UiFile (Join-Path $root 'assets/app.ico') 'assets/app.ico'
    Stage-UiFile (Join-Path $root 'licenses/Qt-Native-runtime.txt') 'licenses/Qt-Native-runtime.txt'
    $sources=Join-Path $root 'build/corresponding-sources'
    New-Item -ItemType Directory -Force -Path $sources | Out-Null
    Copy-Item -LiteralPath $sdk.SourceArchive,$sdk.ImageSourceArchive,$sdk.MediaSourceArchive,$sdk.FFmpegSourceArchive -Destination $sources -Force
    Stage-UiFile (Join-Path $QtRoot 'light-block-sdk-sources.json') 'licenses/qt/sdk-sources.json'
    foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $sdk.SourceDirectory 'LICENSES') -File)) { Stage-UiFile $file.FullName ('licenses/qt/LICENSES/'+$file.Name) }
    # Keep upstream copyright notices and metadata, not only the LGPL text.
    foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $sdk.SourceDirectory 'src/3rdparty') -Recurse -File | Where-Object { $_.Name -match '(?i)^(license|copying|copyright|notice|authors|qt_attribution)' -or $_.Name -match '(?i)(-license|\.license)(\.txt)?$' })) {
        $relative = $file.FullName.Substring($sdk.SourceDirectory.Length+1)
        Stage-UiFile $file.FullName ('licenses/qt/'+$relative)
    }
    foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $sdk.ImageSourceDirectory 'LICENSES') -File)) { Stage-UiFile $file.FullName ('licenses/qtimageformats/LICENSES/'+$file.Name) }
    foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $sdk.ImageSourceDirectory 'src/3rdparty') -Recurse -File | Where-Object { $_.Name -match '(?i)^(license|copying|copyright|notice|authors|qt_attribution)' -or $_.Name -match '(?i)(-license|\.license)(\.txt)?$' })) {
        $relative = $file.FullName.Substring($sdk.ImageSourceDirectory.Length+1)
        Stage-UiFile $file.FullName ('licenses/qtimageformats/'+$relative)
    }
    foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $sdk.MediaSourceDirectory 'LICENSES') -File)) { Stage-UiFile $file.FullName ('licenses/qtmultimedia/LICENSES/'+$file.Name) }
    foreach ($file in (Get-ChildItem -LiteralPath $sdk.MediaSourceDirectory -Recurse -File | Where-Object { $_.Name -match '(?i)^(license|copying|copyright|notice|authors|qt_attribution)' -or $_.Name -match '(?i)(-license|\.license)(\.txt)?$' })) { $relative=$file.FullName.Substring($sdk.MediaSourceDirectory.Length+1);Stage-UiFile $file.FullName ('licenses/qtmultimedia/'+$relative) }
    foreach ($file in (Get-ChildItem -LiteralPath $sdk.FFmpegSourceDirectory -File | Where-Object { $_.Name -match '^(COPYING|LICENSE|CREDITS)' })) { Stage-UiFile $file.FullName ('licenses/qt-ffmpeg/'+$file.Name) }
    Stage-UiFile (Join-Path $root 'tools/build_qt_ffmpeg.sh') 'licenses/qt-ffmpeg/build_qt_ffmpeg.sh'
    foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $QtRoot 'sbom') -File)) { Stage-UiFile $file.FullName ('licenses/qt/sbom/'+$file.Name) }
    $mingw = Split-Path (Split-Path $Compiler)
    foreach ($component in @('gcc','mingw-w64','winpthreads')) {
        foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $mingw ('licenses/'+$component)) -Recurse -File)) {
            $relative = $file.FullName.Substring((Join-Path $mingw 'licenses').Length+1)
            Stage-UiFile $file.FullName ('licenses/qt/mingw/'+$relative)
        }
    }
    Stage-UiFile (Join-Path $mingw 'build-info.txt') 'licenses/qt/mingw/build-info.txt'
    $qtConf = Join-Path $root 'build/qt.conf'
    Set-Content -LiteralPath $qtConf -Value '[Paths]','Plugins=qt-plugins' -Encoding ascii
    Stage-UiFile $qtConf 'qt.conf'
    # Publish the pointer only after the DLL, dependencies and notices are present.
    $manifest = @{schema=1;qt_version='6.8.3';compiler='GCC 13.1.0';ui_dll=$uiName;files=@($files.ToArray())}
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $dist 'ui-runtime.json') -Encoding utf8
    $pointer = Join-Path $dist 'ui-current.txt'
    $pending = $pointer+'.next'
    Set-Content -LiteralPath $pending -Value $uiName -Encoding ascii
    # Antivirus/indexing can briefly hold a newly staged file. Keep the previous
    # complete runtime selected until the atomic replacement succeeds.
    for ($attempt=0; $attempt -lt 6; $attempt++) {
        try {
            if (Test-Path -LiteralPath $pointer) { [IO.File]::Replace($pending,$pointer,[NullString]::Value) } else { [IO.File]::Move($pending,$pointer) }
            break
        } catch [IO.IOException] {
            if ($attempt -eq 5) { throw }
            Start-Sleep -Milliseconds 150
        }
    }
    Write-Output (Join-Path $dist $uiName)
}
finally { $env:PATH = $oldPath; $guard.Dispose() }
