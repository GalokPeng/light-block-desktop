param([string]$CompilerRoot='', [switch]$Offline)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if ($CompilerRoot) {
    $CompilerRoot=[IO.Path]::GetFullPath($CompilerRoot)
    if (-not (Test-Path -LiteralPath (Join-Path $CompilerRoot 'bin/clang++.exe'))) { throw 'CompilerRoot must contain LLVM-MinGW clang++.exe.' }
    Write-Output $CompilerRoot
    return
}
$cache=Join-Path $root 'build/native-toolchain'
$compiler=Join-Path $cache 'Compiler'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$guard=[IO.File]::Open((Join-Path $cache 'setup.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    $base='https://github.com/ramensoftware/windhawk-dependencies/releases/download/v1.6'
    $archives=@(
        @{Name='windhawk-compiler-1.6.7z';SHA256='16352e7dfa65d092af7b7113ddf6cae695e3678da500e611828c1bf2a7d9a2d0';Destination=$compiler},
        @{Name='winrt_winui2_2.8.7_winui3_1.7.250401001.7z';SHA256='43870ab78640c7c62d1825a3d39532f3bf78a4fe3895173a1615f521c82f55b3';Destination=(Join-Path $compiler 'include/winrt')}
    )
    $seven=Join-Path $env:ProgramFiles '7-Zip/7z.exe'
    if (-not (Test-Path -LiteralPath $seven)) { $seven=(Get-Command 7z.exe -ErrorAction Stop).Source }
    foreach ($archive in $archives) {
        $file=Join-Path $cache $archive.Name
        if (-not (Test-Path -LiteralPath $file)) {
            if ($Offline) { throw ('Missing toolchain cache: '+$file) }
            & curl.exe -fsSL --retry 3 --connect-timeout 20 --max-time 300 "$base/$($archive.Name)" -o ($file+'.partial')
            if ($LASTEXITCODE -ne 0) { throw ('Toolchain download failed: '+$archive.Name) }
            if ((Get-FileHash -LiteralPath ($file+'.partial')).Hash -ine $archive.SHA256) { throw 'Toolchain download checksum mismatch.' }
            Move-Item -LiteralPath ($file+'.partial') -Destination $file
        }
        if ((Get-FileHash -LiteralPath $file).Hash -ine $archive.SHA256) { throw ('Cached toolchain checksum mismatch: '+$file) }
        $marker=Join-Path $cache ($archive.Name+'.ready')
        if (-not (Test-Path -LiteralPath $marker)) {
            New-Item -ItemType Directory -Force -Path $archive.Destination | Out-Null
            & $seven x -y -bd -bso0 ('-o'+$archive.Destination) $file
            if ($LASTEXITCODE -ne 0) { throw ('Toolchain extraction failed: '+$archive.Name) }
            Set-Content -LiteralPath $marker -Value $archive.SHA256 -Encoding ascii
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $compiler 'bin/clang++.exe')) -or -not (Test-Path -LiteralPath (Join-Path $compiler 'include/winrt/base.h'))) { throw 'The pinned toolchain is incomplete.' }
    $archives | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $cache 'sources.json') -Encoding utf8
    Write-Output $compiler
} finally { $guard.Dispose() }
