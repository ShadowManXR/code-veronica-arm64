param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
Push-Location $root
try {
    if(!$SkipBuild){
        & cmake --build arm64/build --target recvx_android
        if($LASTEXITCODE -ne 0){throw 'Build the native app objects first.'}
    }
    $toolchain=(Select-String -LiteralPath arm64/build/CMakeCache.txt -Pattern '^CMAKE_TOOLCHAIN_FILE:FILEPATH=').Line.Split('=',2)[1]
    $ndk=Split-Path (Split-Path (Split-Path $toolchain))
    $clang=Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    $objects=Get-ChildItem arm64/build/CMakeFiles/recvx_android.dir -Recurse -Filter '*.o' |
        Where-Object {$_.Name -ne 'native_activity.c.o'} | ForEach-Object {Resolve-Path -Relative $_.FullName}
    & $clang --target=aarch64-none-linux-android29 -std=gnu11 -DRECVX_NATIVE_ROOM -DRECVX_NATIVE_ROOM_SCRIPTS -fsigned-char -O2 -ffp-contract=off -Iarm64 -Iarm64/android -Iport/pc/src -Iport/pc/src/ps2 -isystem port/pc/include -isystem port/pc/include/sdk -isystem port/decomp/include/ps2/veronica/prog -isystem port/decomp/src/cri/mwlib/ee/lib/libadxe -idirafter port/decomp/include/recvx-decomp-katana/KATANA/Include -idirafter port/decomp/include/recvx-decomp-cri/cri/mwlib/include -idirafter port/decomp/include/recvx-decomp-cri/cri/mwlib/ee/include arm64/test_enemy_task_device.c @objects arm64/build/librecvx_egl.a '-Wl,--gc-sections' -laaudio -llog -lEGL -lGLESv3 -landroid -lm -o arm64/build/check-enemy-task-arm64
    if($LASTEXITCODE -ne 0){throw 'Could not link native original enemy task checks.'}
} finally {Pop-Location}
