# Code: Veronica X — Android ARM64 / Quest 3

An in-progress native ARM64 port of the original Resident Evil Code: Veronica X
engine, targeting Meta Quest 3. The original 4:3 game is displayed on a screen
inside an OpenXR virtual room, with Quest Touch input routed through the original
gamepad controls. This is not a completed campaign port.

## Progress — 23 November 2026

The latest installed and manually checked app is **v0.86**. The repository also
contains newer inventory components verified in standalone native device tests;
these have **not** been enabled as an interactive inventory in the app.

| Area | Verified progress | Remaining work |
| --- | --- | --- |
| ARM64 foundation | Checked native pointers, guest layouts, game arena ownership, EE/DMA and portable VU execution | Remaining pointer-heavy engine work and whole-runtime acceptance |
| Disc and resources | Read-only PS2 ISO/GDFS/archive loading, room/model/texture/motion conversion | Campaign-wide resource and lifetime coverage |
| Quest host and menus | OpenXR theater, stereo screen, direct Touch input, original title/options/New Game entry | Complete original Load Game and gameplay save lifecycle |
| Movies and audio | Opening movie reaches the title with synchronized sound; event movie/audio components have native checks | Continuous original cutscene sequencing and campaign audio acceptance |
| Player and rendering | Starting room, Claire's original model/hands/motion, walking, running, quick-turn and shadow | Complete combat, head/damage/contact behavior and scripted opening interactions |
| Enemies | Original model/motion/work ownership, spawning and selected linked-part initializers | Full hostile initialization/MainLoop, perception, attacks, damage/death, collision and linked-part controllers |
| Inventory (unreleased) | Original request/entry, owned five-atlas package, layouts, item sorting/icons/quantities, item names, cursor/top-menu navigation, help, portrait/box visibility and background fades | Complete frame ordering/composition/cache ownership, health animation, item actions and safe return to gameplay; file/map/item-box/combine tasks |
| Campaign | Original starting-state and room setup plus isolated script/controller tests | Entire opening, player-control handoff, first authored room transition, save/load, then campaign-through-ending validation |

**Latest device verification:** all six native suites and **51 freshly linked
integration helpers** passed on Quest 3. The newest checks cover 16,512 original
portrait/box visibility cases, entry setup, ordered background fades and
no-write rejection of invalid state. Existing inventory initialization, GPU,
player, camera, script, movie and audio regressions also pass. These results
verify bounded components; they do not establish a full playable campaign.

This work adapts original source routines and data semantics from the upstream
port/decompilation. Android, OpenXR, GLES, ownership validation and transport
code provide the native platform integration. Unsupported original runtime
paths remain gated rather than being counted as completed gameplay.

## Next milestones

1. Finish the original inventory screen, actions and close/restore path, then
   enable the live inventory call after pause/return verification.
2. Finish original enemy animation/controller dispatch and hostile behavior.
3. Integrate player head control, damage reactions, enemy contact, combat and
   opening-script interactions with the per-frame player controller.
4. Complete camera, effects and opening cutscene sequencing in the live runtime.
5. Validate New Game through the entire opening, control handoff, first room
   transition and save/load; then enable the original gameplay runtime and expand
   testing across the campaign.

Detailed historical results and gates: [ARM64 README](arm64/README.md) and
[port plan](arm64/PORT_PLAN.md). Earlier roadmap entries describe the project's
previous flat-only scope; the current presentation uses the virtual room.

## Build

Requirements: Git, Python 3, Android NDK (r27d tested), CMake, Ninja, Android SDK,
Java and PowerShell for the packaging/device scripts. The Khronos OpenXR 1.1.54
headers and Android ARM64 loader are included with their license.

Clone with dependencies, then apply the required native-width SDK-header patch:

```powershell
git clone --recurse-submodules https://github.com/ShadowManXR/code-veronica-arm64.git
cd code-veronica-arm64
& ./Prepare-Dependencies.ps1
cmake -S arm64 -B arm64/build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=<NDK>/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
cmake --build arm64/build
& ./arm64/Build-Apk.ps1
```

`Build-Apk.ps1` accepts explicit SDK, Java, platform, build-tools and debug-keystore
paths; see the [ARM64 build instructions](arm64/README.md#build-and-verify).
Only Android ARM64 is the target of this repository's current validation.

## Game data and installation

Supply your own PS2 NTSC-U **SLUS-20184** disc image. No ISO, extracted game
assets, saves, SDK toolchains or signing keys are included. After building:

```powershell
adb install -r arm64/build/recvx-arm64-host.apk
adb shell mkdir -p /sdcard/Android/obb/org.recvx.arm64
adb push '<path to your SLUS-20184 ISO>' /sdcard/Android/obb/org.recvx.arm64/game.iso
adb shell am start -n org.recvx.arm64/android.app.NativeActivity
```

Keep one supported ISO in that OBB directory. Wake the headset and Touch
controllers, accepting Quest's controller prompt if shown. A skips the startup
movie/confirms menus; left stick provides original movement and B is Run under
the default Type A configuration. Back + Run performs the original quick-turn.
This build still exposes a partial original runtime, not a campaign-ready game.

## Verification

```powershell
& ./arm64/Test-OnQuest.ps1 -Adb '<path to adb.exe>' -Serial '<Quest serial>'
```

The default EE/DMA and VU checks do not need an ISO. Optional fixtures require
locally supplied game data and must remain untracked; see the ARM64 README.
`arm64/Build-*Check.ps1` builds the individual integration helpers after the
native app objects. Disc-backed helpers receive the device's OBB directory.
Generated executables, logs and fixtures are excluded from Git.

## Source layout and provenance

- `arm64/`: native Android host, checked adapters, tests, build scripts and plan.
- `port/pc/`: upstream host/shared engine code with the current ARM64 changes.
- `port/decomp/`: original-source decompilation snapshot with current changes.
- `port/decomp/include/recvx-decomp-katana` and `recvx-decomp-cri`: pinned external
  SDK-header dependencies; the required Katana change is in `patches/`.

The port and decompilation are stored as source in this repository so local
changes are included. Their previous nested Git histories and unused legacy
compiler binaries are not bundled. Upstream references are recorded in
[SOURCE_PROVENANCE.md](SOURCE_PROVENANCE.md). The separate historical replacement
VR prototype and its rollback archive are not part of this original-engine port.

This is AI-assisted development. Preserve upstream authorship and license
notices. See [LICENSE](LICENSE), [third-party notices](THIRD_PARTY_NOTICES.md),
and the licenses within the source/dependency directories. Resident Evil and
Code: Veronica belong to their respective owners; this project is unaffiliated
with Capcom.
