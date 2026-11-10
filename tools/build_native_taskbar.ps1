param([string]$ClangPath = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'native/taskbar_layout.cpp'
$target = Join-Path $root 'build/native-taskbar'
New-Item -ItemType Directory -Force -Path $target | Out-Null

if (-not $ClangPath) {
    $cache = Join-Path $root 'build/native-toolchain'
    $compiler = Join-Path $cache 'Compiler'
    $ClangPath = Join-Path $compiler 'bin/clang++.exe'
    $ready = Join-Path $compiler 'qingkuai-v1.6.ready'
    if (-not (Test-Path -LiteralPath $ready -PathType Leaf)) {
        $headers = Join-Path $compiler 'include/winrt'
        New-Item -ItemType Directory -Force -Path $headers | Out-Null
        $base = 'https://github.com/ramensoftware/windhawk-dependencies/releases/download/v1.6'
        $archives = @(
            @{ Name='windhawk-compiler-1.6.7z'; SHA256='16352E7DFA65D092AF7B7113DDF6CAE695E3678DA500E611828C1BF2A7D9A2D0'; Destination=$compiler },
            @{ Name='winrt_winui2_2.8.7_winui3_1.7.250401001.7z'; SHA256='43870AB78640C7C62D1825A3D39532F3BF78A4FE3895173A1615F521C82F55B3'; Destination=$headers }
        )
        $sevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
        foreach ($archive in $archives) {
            $path = Join-Path $cache $archive.Name
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                & curl.exe -fsSL --retry 3 --max-time 180 "$base/$($archive.Name)" -o $path
                if ($LASTEXITCODE -ne 0) { throw "Could not download $($archive.Name)." }
            }
            if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $archive.SHA256) {
                throw "Checksum mismatch for $path. Remove the cached file before retrying."
            }
            if (Test-Path -LiteralPath $sevenZip -PathType Leaf) {
                & $sevenZip x -y -bd -bso0 "-o$($archive.Destination)" $path
            } else {
                & tar.exe -xf $path -C $archive.Destination
            }
            if ($LASTEXITCODE -ne 0) { throw "Could not extract $($archive.Name)." }
        }
        if (-not (Test-Path -LiteralPath $ClangPath -PathType Leaf)) { throw 'The native compiler archive is incomplete.' }
        Set-Content -LiteralPath $ready -Value 'LLVM 20.1.3' -Encoding ascii
    }
}

$hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.Substring(0,12).ToLowerInvariant()
$name = "QingkuaiTaskbarLayout-$hash.dll"
$output = Join-Path $target $name
if (-not (Test-Path -LiteralPath $output)) {
    & $ClangPath --target=x86_64-w64-mingw32 -std=c++23 -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 `
        -Wno-dll-attribute-on-redeclaration -shared -static -O2 $source -o $output -ladvapi32 -lole32 -loleaut32 -lruntimeobject -luuid
    if ($LASTEXITCODE -ne 0) { throw 'Could not build the native taskbar layout.' }
}
Set-Content -LiteralPath (Join-Path $target 'current.txt') -Value $name -Encoding ascii
Write-Output $output
