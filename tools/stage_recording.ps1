param([string]$RuntimeDirectory='', [string]$DestinationDirectory='', [string]$PythonPath='python', [switch]$Offline)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$priorOffline=$env:QINGKUAI_BUILD_OFFLINE
$priorPythonUtf8=$env:PYTHONUTF8
try {
    $env:PYTHONUTF8='1'
    if ($Offline) { $env:QINGKUAI_BUILD_OFFLINE='1' }
    if (-not $RuntimeDirectory) {
        $binary=& $PythonPath (Join-Path $PSScriptRoot 'build_recording_runtime.py')
        if ($LASTEXITCODE -ne 0) { throw 'Pinned recording runtime build failed.' }
        $RuntimeDirectory=Split-Path ([string]$binary)
    }
    $RuntimeDirectory=[IO.Path]::GetFullPath($RuntimeDirectory)
    $binary=Join-Path $RuntimeDirectory 'ffmpeg.exe';$source=Join-Path $RuntimeDirectory 'ffmpeg-source.zip'
    foreach ($file in @($binary,$source)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw ('Incomplete recording runtime: '+$file) } }
    & $PythonPath (Join-Path $PSScriptRoot 'validate_recording_runtime.py') $binary
    if ($LASTEXITCODE -ne 0) { throw 'Recording runtime is missing required capture/audio/export capabilities.' }
    $binaryHash=(Get-FileHash -LiteralPath $binary).Hash.ToLowerInvariant()
    $sourceHash=(Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant()
    $manifestPath=Join-Path $RuntimeDirectory 'build-manifest.json'
    if (Test-Path -LiteralPath $manifestPath) {
        $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if ($manifest.schema -ne 1 -or $manifest.binary_sha256 -cne $binaryHash -or $manifest.source_sha256 -cne $sourceHash) { throw 'Recording runtime integrity mismatch.' }
    } elseif ($binaryHash -cne '0a79c6f218c1dc7c4b838717f8cc7bf0be5ff50376af9e8b08579a858163504c' -or $sourceHash -cne '8332c10f2d4dc9592670edf14c5fa673f1d6b52dba546aa689b7bbcef954bfdd') {
        throw 'External runtime needs a matching build manifest; unverified binary rejected.'
    }
    if (-not $DestinationDirectory) { $DestinationDirectory=Join-Path $root 'dist/LightBlockDesktopPreview/runtime/recording' }
    $sources=Join-Path $root 'build/corresponding-sources'
    New-Item -ItemType Directory -Force -Path $DestinationDirectory,$sources | Out-Null
    Copy-Item -LiteralPath $binary -Destination (Join-Path $DestinationDirectory 'ffmpeg.exe') -Force
    Copy-Item -LiteralPath $source -Destination (Join-Path $sources 'ffmpeg-source.zip') -Force
    Copy-Item -LiteralPath (Join-Path $root 'licenses/FFmpeg-Native-runtime.txt'),(Join-Path $root 'licenses/FFmpeg-GPL-3.0.txt') -Destination $DestinationDirectory -Force
    $identity=Join-Path $root 'build/runtime_identity.hpp'
    $contents="#pragma once`n#define LB_FFMPEG_SHA256 L`"$binaryHash`"`n"
    if (-not (Test-Path -LiteralPath $identity) -or (Get-Content -LiteralPath $identity -Raw) -cne $contents) { [IO.File]::WriteAllText($identity,$contents,[Text.UTF8Encoding]::new($false)) }
    $version=(& $PythonPath (Join-Path $PSScriptRoot 'release.py') version).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Could not read release version.' }
    @{schema=1;binary_sha256=$binaryHash;source_sha256=$sourceHash;source_asset="Qingkuai-Preview-v$version-ffmpeg-source.zip"} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $DestinationDirectory 'build-manifest.json') -Encoding utf8
    Write-Output $DestinationDirectory
} finally { $env:QINGKUAI_BUILD_OFFLINE=$priorOffline;$env:PYTHONUTF8=$priorPythonUtf8 }
