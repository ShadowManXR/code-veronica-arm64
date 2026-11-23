param(
    [Parameter(Mandatory)][string]$Clang,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'),
    [string]$FixtureDirectory = (Join-Path $BuildDirectory 'fixture')
)
$ErrorActionPreference = 'Stop'
# Run from a Visual Studio x86 Developer PowerShell so the Windows SDK,
# CRT headers and x86 linker libraries are available to Clang.
$root = Split-Path -Parent $PSScriptRoot
$fixtureNames = 'opening.mdl', 'opening.mtn', 'opening.expected'
$fixtures = foreach ($name in $fixtureNames) {
    (Resolve-Path -LiteralPath (Join-Path $FixtureDirectory $name)).Path
}
$output = Join-Path $BuildDirectory 'recvx_opening_reference_x86.exe'
& $Clang --target=i686-pc-windows-msvc -fuse-ld=link -std=c11 -fsigned-char `
    -D_CRT_SECURE_NO_WARNINGS -DRECVX_ORIGINAL_LOADER_ORACLE `
    -I "$root/port/decomp/include/ps2/veronica/prog" `
    -idirafter "$root/port/decomp/include/recvx-decomp-katana/KATANA/Include" `
    "$PSScriptRoot/test_opening.c" "$PSScriptRoot/ninja_binary.c" `
    "$PSScriptRoot/original_loader_oracle.c" -o $output
if ($LASTEXITCODE -ne 0) { throw 'Original loader reference build failed' }
& $output @fixtures
if ($LASTEXITCODE -ne 0) { throw 'Original loader reference check failed' }
