param([string]$QtRoot = '', [string]$DownloadsDirectory = '', [switch]$Offline)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $QtRoot) { $QtRoot = Join-Path $root 'build/qt-sdk' }
if (-not $DownloadsDirectory) { $DownloadsDirectory = Join-Path $root 'build/qt-downloads' }
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
$DownloadsDirectory = [IO.Path]::GetFullPath($DownloadsDirectory)
$sourceDirectory = Join-Path $root 'build/qt-source'
$archiveName = '6.8.3-0-202503201308qtbase-Windows-Windows_10_22H2-Mingw-Windows-Windows_10_22H2-X86_64.7z'
$imageArchiveName = '6.8.3-0-202503201308qtimageformats-Windows-Windows_10_22H2-Mingw-Windows-Windows_10_22H2-X86_64.7z'
$mediaArchiveName = '6.8.3-0-202503201308qtmultimedia-Windows-Windows_10_22H2-Mingw-Windows-Windows_10_22H2-X86_64.7z'
$archives = @(
    @{name=$archiveName;sha256='a2cd09053e907b161d84696ec5f1c316cfc089482c5b04de9141b949fa19acd1';url=('https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.win64_mingw/'+$archiveName)},
    @{name='mingw1310.7z';sha256='a7b502294a903b64fccd4a41ba39f48d3f6c9a5ece167f0cfaeac920d15b344f';url='https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/tools_mingw1310/qt.tools.win64_mingw1310/13.1.0-202407240918mingw1310.7z'},
    @{name='qtbase-everywhere-src-6.8.3.tar.xz';sha256='56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80';url='https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz'},
    @{name=$imageArchiveName;sha256='8ca361259b8c9a72ad9117e064ac6fbef58342002b4eed1ea27678dbe560f0c8';url=('https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.addons.qtimageformats.win64_mingw/'+$imageArchiveName)},
    @{name='qtimageformats-everywhere-src-6.8.3.tar.xz';sha256='049bfb99845e4801672aca07c3c4fc4c140f932a3a33faa899419579e33ef1c8';url='https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtimageformats-everywhere-src-6.8.3.tar.xz'},
    @{name=$mediaArchiveName;sha256='043704cb7d5c6584637b45710d035d09a247dd1fafac9429a3bd668ea55c8ed6';url=('https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.addons.qtmultimedia.win64_mingw/'+$mediaArchiveName)},
    @{name='qtmultimedia-everywhere-src-6.8.3.tar.xz';sha256='32e82307d783172a3b984cc3c47c5e4e8b819cee3cbfc702c7012c47f15f6b01';url='https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtmultimedia-everywhere-src-6.8.3.tar.xz'},
    @{name='FFmpeg-n7.1.tar.gz';sha256='7ddad2d992bd250a6c56053c26029f7e728bebf0f37f80cf3f8a0e6ec706431a';url='https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.tar.gz'}
)
$sevenZip = (Get-Command 7z.exe -ErrorAction SilentlyContinue).Source
if (-not $sevenZip) { $sevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe' }
if (-not (Test-Path -LiteralPath $sevenZip)) { throw 'Install 7-Zip or put 7z.exe on PATH to extract the local Qt SDK.' }
New-Item -ItemType Directory -Force -Path $QtRoot,$DownloadsDirectory,$sourceDirectory | Out-Null
$guard = [IO.File]::Open((Join-Path $root 'build/qt-sdk.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    foreach ($archive in $archives) {
        $file = Join-Path $DownloadsDirectory $archive.name
        if (-not (Test-Path -LiteralPath $file)) {
            if ($Offline) { throw ('Missing cached Qt archive in offline mode: '+$file) }
            $partial = $file+'.partial'
            & curl.exe -fsSL --retry 3 --connect-timeout 20 --max-time 600 $archive.url -o $partial
            if ($LASTEXITCODE -ne 0) { throw ('Qt archive download failed: '+$archive.name) }
            if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archive.sha256) { throw ('Downloaded archive hash mismatch: '+$archive.name) }
            Move-Item -LiteralPath $partial -Destination $file
        }
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archive.sha256) { throw ('Cached archive hash mismatch: '+$file) }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'bin/Qt6Widgets.dll'))) {
        & $sevenZip x -y ('-o'+$QtRoot) (Join-Path $DownloadsDirectory $archiveName) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Qt SDK extraction failed.' }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'Tools/mingw1310_64/bin/g++.exe'))) {
        & $sevenZip x -y ('-o'+$QtRoot) (Join-Path $DownloadsDirectory 'mingw1310.7z') | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'MinGW SDK extraction failed.' }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'plugins/imageformats/qwebp.dll')) -or -not (Test-Path -LiteralPath (Join-Path $QtRoot 'plugins/imageformats/qtiff.dll'))) {
        & $sevenZip x -y ('-o'+$QtRoot) (Join-Path $DownloadsDirectory $imageArchiveName) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Qt Image Formats SDK extraction failed.' }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'plugins/multimedia/ffmpegmediaplugin.dll'))) {
        & $sevenZip x -y ('-o'+$QtRoot) (Join-Path $DownloadsDirectory $mediaArchiveName) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Qt Multimedia SDK extraction failed.' }
    }
    $qtSource = Join-Path $sourceDirectory 'qtbase-everywhere-src-6.8.3'
    if (-not (Test-Path -LiteralPath (Join-Path $qtSource '.light-block-extracted'))) {
        & $sevenZip x -y ('-o'+$sourceDirectory) (Join-Path $DownloadsDirectory 'qtbase-everywhere-src-6.8.3.tar.xz') | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Qt source decompression failed.' }
        & $sevenZip x -y ('-o'+$sourceDirectory) (Join-Path $sourceDirectory 'qtbase-everywhere-src-6.8.3.tar') | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Qt source extraction failed.' }
        Set-Content -LiteralPath (Join-Path $qtSource '.light-block-extracted') -Value $archives[2].sha256 -Encoding ascii
    }
    $imageSource = Join-Path $sourceDirectory 'qtimageformats-everywhere-src-6.8.3'
    if (-not (Test-Path -LiteralPath (Join-Path $imageSource '.light-block-extracted'))) {
        & $sevenZip x -y ('-o'+$sourceDirectory) (Join-Path $DownloadsDirectory 'qtimageformats-everywhere-src-6.8.3.tar.xz') | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Qt Image Formats source decompression failed.' }
        & $sevenZip x -y ('-o'+$sourceDirectory) (Join-Path $sourceDirectory 'qtimageformats-everywhere-src-6.8.3.tar') | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Qt Image Formats source extraction failed.' }
        Set-Content -LiteralPath (Join-Path $imageSource '.light-block-extracted') -Value $archives[4].sha256 -Encoding ascii
    }
    $archives | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $QtRoot 'light-block-sdk-sources.json') -Encoding utf8
    $mediaSource=Join-Path $sourceDirectory 'qtmultimedia-everywhere-src-6.8.3'
    $ffmpegSource=Join-Path $sourceDirectory 'FFmpeg-n7.1'
    foreach ($source in @(@{file='qtmultimedia-everywhere-src-6.8.3.tar.xz';directory=$mediaSource;tar='qtmultimedia-everywhere-src-6.8.3.tar'},@{file='FFmpeg-n7.1.tar.gz';directory=$ffmpegSource;tar='FFmpeg-n7.1.tar'})) {
        if (-not (Test-Path -LiteralPath (Join-Path $source.directory '.light-block-extracted'))) {
            & $sevenZip x -y ('-o'+$sourceDirectory) (Join-Path $DownloadsDirectory $source.file) | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Media source decompression failed.' }
            & $sevenZip x -y ('-o'+$sourceDirectory) (Join-Path $sourceDirectory $source.tar) | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Media source extraction failed.' }
            Set-Content -LiteralPath (Join-Path $source.directory '.light-block-extracted') -Value $source.file -Encoding ascii
        }
    }
    [pscustomobject]@{QtRoot=$QtRoot;Compiler=(Join-Path $QtRoot 'Tools/mingw1310_64/bin/g++.exe');SourceDirectory=$qtSource;SourceArchive=(Join-Path $DownloadsDirectory 'qtbase-everywhere-src-6.8.3.tar.xz');ImageSourceDirectory=$imageSource;ImageSourceArchive=(Join-Path $DownloadsDirectory 'qtimageformats-everywhere-src-6.8.3.tar.xz');MediaSourceDirectory=$mediaSource;MediaSourceArchive=(Join-Path $DownloadsDirectory 'qtmultimedia-everywhere-src-6.8.3.tar.xz');FFmpegSourceDirectory=$ffmpegSource;FFmpegSourceArchive=(Join-Path $DownloadsDirectory 'FFmpeg-n7.1.tar.gz')}
}
finally { $guard.Dispose() }
