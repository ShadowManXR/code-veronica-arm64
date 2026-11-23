$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    & git submodule update --init --recursive
    if($LASTEXITCODE -ne 0){throw 'Could not initialize the pinned SDK-header dependencies.'}
    $katana=Join-Path $PSScriptRoot 'port/decomp/include/recvx-decomp-katana'
    $patch=Join-Path $PSScriptRoot 'patches/katana-native-texture-pointer.patch'
    & git -C $katana apply --reverse --check $patch 2>$null
    if($LASTEXITCODE -eq 0){Write-Output 'Native SDK-header patch is already applied.';return}
    & git -C $katana apply --check $patch
    if($LASTEXITCODE -ne 0){throw 'SDK headers differ from the pinned version; inspect before applying the native patch.'}
    & git -C $katana apply $patch
    if($LASTEXITCODE -ne 0){throw 'Could not apply the native-width texture pointer patch.'}
    Write-Output 'Applied native SDK-header patch.'
} finally {Pop-Location}
