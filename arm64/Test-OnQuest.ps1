param(
    [Parameter(Mandatory)][string]$Adb,
    [Parameter(Mandatory)][string]$Serial,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'),
    [string]$FixtureDirectory
)
$ErrorActionPreference = 'Stop'
function Invoke-DeviceAdb {
    & $Adb -s $Serial @args
    if ($LASTEXITCODE -ne 0) { throw "ADB command failed: $LASTEXITCODE" }
}

$binaries = 'recvx_check_ee', 'recvx_check_vu'
$fixtures = @()
if ($FixtureDirectory) {
    $binaries += 'recvx_check_opening', 'recvx_check_room', 'recvx_check_events', 'recvx_check_effects'
    $fixtures = 'opening.mdl', 'opening.mtn', 'opening.expected', 'opening.rdt', 'opening.bank', 'opening.rom',
                'routes.rdt', 'routes.rom', 'lights.rdt', 'lights.rom', 'common-effects.bin', 'random.expected'
}
foreach ($name in $binaries) {
    if (!(Test-Path -LiteralPath (Join-Path $BuildDirectory $name) -PathType Leaf)) {
        throw "Build $name before running the device tests."
    }
}
foreach ($name in $fixtures) {
    if (!(Test-Path -LiteralPath (Join-Path $FixtureDirectory $name) -PathType Leaf)) {
        throw "Generate $name with opening_fixture.py before running the opening check."
    }
}
$abi = (Invoke-DeviceAdb shell getprop ro.product.cpu.abi).Trim()
if ($abi -ne 'arm64-v8a') { throw "Expected arm64-v8a, got $abi" }
$remote = '/data/local/tmp/recvx-arm64-' + [guid]::NewGuid().ToString('N')
Invoke-DeviceAdb shell mkdir $remote
try {
    foreach ($fixture in $fixtures) {
        Invoke-DeviceAdb push (Join-Path $FixtureDirectory $fixture) "$remote/$fixture"
    }
    foreach ($name in $binaries) {
        Invoke-DeviceAdb push (Join-Path $BuildDirectory $name) "$remote/$name"
        Invoke-DeviceAdb shell chmod 700 "$remote/$name"
        if ($name -eq 'recvx_check_opening') {
            Invoke-DeviceAdb shell "$remote/$name" "$remote/opening.mdl" "$remote/opening.mtn" "$remote/opening.expected"
        } elseif ($name -eq 'recvx_check_room') {
            Invoke-DeviceAdb shell "RECVX_VU_CHECK=1" "$remote/$name" "$remote/opening.rdt" "$remote/opening.bank" "$remote/opening.rom"
            Invoke-DeviceAdb shell "$remote/$name" --header-only "$remote/routes.rdt" "$remote/routes.rom"
            Invoke-DeviceAdb shell "$remote/$name" --lights-only "$remote/lights.rdt" "$remote/lights.rom"
            Invoke-DeviceAdb shell "$remote/$name" --effects-only "$remote/common-effects.bin"
            Invoke-DeviceAdb shell "$remote/$name" --room-effects-only "$remote/opening.rdt"
            Invoke-DeviceAdb shell "$remote/$name" --particles-only "$remote/opening.rdt" "$remote/common-effects.bin"
        } elseif ($name -eq 'recvx_check_events') {
            Invoke-DeviceAdb shell "$remote/$name" "$remote/opening.rdt" "$remote/random.expected"
        } elseif ($name -eq 'recvx_check_effects') {
            Invoke-DeviceAdb shell "$remote/$name" "$remote/common-effects.bin"
        } else {
            Invoke-DeviceAdb shell "$remote/$name"
        }
    }
} finally {
    $cleanup = @(($binaries + $fixtures) | ForEach-Object { "$remote/$_" })
    & $Adb -s $Serial shell rm -f @cleanup
    & $Adb -s $Serial shell rmdir $remote
}
