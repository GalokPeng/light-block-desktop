param([string]$QtRoot='', [string]$Compiler='', [switch]$Offline)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $QtRoot -or -not $Compiler) { $sdk=& (Join-Path $PSScriptRoot 'setup_ui_sdk.ps1') -QtRoot $QtRoot -Offline:$Offline; $QtRoot=$sdk.QtRoot;$Compiler=$sdk.Compiler }
$oldPath=$env:PATH
try {
    $env:PATH=(Split-Path $Compiler)+';'+$oldPath
    $compilerVersion=(& $Compiler -dumpfullversion).Trim()
    if ($LASTEXITCODE -ne 0 -or $compilerVersion -ne '13.1.0') { throw 'Qt 6.8.3 media worker requires its matching GCC 13.1.0 ABI.' }
    $output=Join-Path $root 'build/LightBlockMedia.next.dll'
    $source=Join-Path $root 'ui/media_worker.cpp';$header=Join-Path $root 'src/media_worker_api.h'
    $signature=(Get-FileHash -LiteralPath $source).Hash+(Get-FileHash -LiteralPath $header).Hash+(Get-FileHash -LiteralPath $Compiler).Hash+(Get-FileHash -LiteralPath (Join-Path $QtRoot 'bin/Qt6Core.dll')).Hash+'-qt683-cxx23-v1'
    $stamp=Join-Path $root 'build/media-ui-signature.txt'
    if (-not (Test-Path -LiteralPath $output) -or -not (Test-Path -LiteralPath $stamp) -or (Get-Content -LiteralPath $stamp -Raw).Trim() -cne $signature) {
        $includes=@('include','include/QtWidgets','include/QtGui','include/QtCore','include/QtMultimedia','include/QtMultimediaWidgets','include/QtOpenGLWidgets','include/QtOpenGL') | ForEach-Object { '-I'+(Join-Path $QtRoot $_) }
        & $Compiler -std=c++23 -O2 -shared -Wall -Wextra -Werror -Wno-misleading-indentation -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 @includes $source ('-L'+(Join-Path $QtRoot 'lib')) -o $output -lQt6Multimedia -lQt6MultimediaWidgets -lQt6OpenGLWidgets -lQt6OpenGL -lQt6Widgets -lQt6Gui -lQt6Core -luser32 -lshell32
        if ($LASTEXITCODE -ne 0) { throw 'Isolated Qt media worker compilation failed.' }
        Set-Content -LiteralPath $stamp -Value $signature -Encoding ascii
    }
    Write-Output $output
} finally { $env:PATH=$oldPath }
