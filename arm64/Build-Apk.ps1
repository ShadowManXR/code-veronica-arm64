param(
    [string]$Sdk = (Join-Path $env:LOCALAPPDATA 'Android/Sdk'),
    [string]$Platform = 'android-36.1',
    [string]$BuildTools = '37.0.0',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'),
    [string]$Keystore = (Join-Path $env:USERPROFILE '.android/debug.keystore'),
    [string]$Java = 'java'
)
$ErrorActionPreference = 'Stop'

function Invoke-BuildTool([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}

$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$tools = Join-Path $Sdk "build-tools/$BuildTools"
$androidJar = Join-Path $Sdk "platforms/$Platform/android.jar"
$signerJar = Join-Path $tools 'lib/apksigner.jar'
$manifest = Join-Path $PSScriptRoot 'android/AndroidManifest.xml'
$library = Join-Path $build 'librecvx_android.so'
foreach ($inputFile in @($androidJar, $signerJar, $manifest, $Keystore)) {
    if (!(Test-Path -LiteralPath $inputFile -PathType Leaf)) {
        throw "Missing APK input $inputFile"
    }
}

Invoke-BuildTool 'cmake' @('--build', $build, '--target', 'recvx_android')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$staging = Join-Path $build ('apk-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging | Out-Null
$base = Join-Path $staging 'base.apk'
$aligned = Join-Path $staging 'aligned.apk'
$signed = Join-Path $staging 'signed.apk'
$output = Join-Path $build 'recvx-arm64-host.apk'
try {
    Invoke-BuildTool (Join-Path $tools 'aapt2.exe') @(
        'link', '-I', $androidJar, '--manifest', $manifest, '-o', $base
    )
    $zip = [System.IO.Compression.ZipFile]::Open($base, [System.IO.Compression.ZipArchiveMode]::Update)
    try {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $library, 'lib/arm64-v8a/librecvx_android.so', [System.IO.Compression.CompressionLevel]::NoCompression
        ) | Out-Null
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, (Join-Path $build 'libopenxr_loader.so'), 'lib/arm64-v8a/libopenxr_loader.so',
            [System.IO.Compression.CompressionLevel]::NoCompression
        ) | Out-Null
    } finally {
        $zip.Dispose()
    }
    Invoke-BuildTool (Join-Path $tools 'zipalign.exe') @('-P', '16', '4', $base, $aligned)
    Invoke-BuildTool $Java @(
        '-jar', $signerJar, 'sign', '--ks', $Keystore, '--ks-key-alias', 'androiddebugkey', '--ks-pass', 'pass:android',
        '--key-pass', 'pass:android', '--out', $signed, $aligned
    )
    Invoke-BuildTool $Java @('-jar', $signerJar, 'verify', $signed)
    Invoke-BuildTool (Join-Path $tools 'zipalign.exe') @('-c', '-P', '16', '4', $signed)
    Move-Item -LiteralPath $signed -Destination $output -Force
    Write-Output $output
} finally {
    foreach ($path in @($base, $aligned, $signed, "$signed.idsig")) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
    Remove-Item -LiteralPath $staging
}
