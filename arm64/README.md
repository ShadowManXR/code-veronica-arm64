# Code: Veronica X on Android ARM64

Target: run the original game engine natively on Quest 3, with its 4:3 picture
on a large screen inside a virtual room. The engine reads the player's PS2 disc
image. The build produces six shell executables for engine checks
and an OpenXR Android host APK. It plays the original startup movie and
presents the title artwork, main menu, New Game difficulty screen, and Options at 4:3.
The host opens the supported PS2 disc image from its OBB directory and runs the
original GDFS initialization. New Game initializes the original starting state
and draws the starting room using the 4:3 renderer.

**Current status (v0.86):** original head, arm, leg and already-linked cap
initializers now run through bounded native work adapters. Their 32/112-byte
scalar layouts and matrix areas are preserved; object/world pointers live in
separate typed storage. Unreferenced inactive linked parts can retire their
native views and recycle through the original first-free constructor. All six
native suites and 44 integration helpers pass. Detached/damage behavior, other
part controllers, full hostile initialization/MainLoop and the complete opening
remain gated. Stage 1 is still in progress; the visible scene is unchanged.

**Unreleased inventory work:** original request arbitration, status-task entry
and normal-inventory initialization now run with checked native S_WORK/SITEM
ownership, original sprite tables, item-bank selection and health pulse setup.
All six native suites and 51 integration helpers pass on Quest for this
working tree, including original cursor/top-menu navigation, system help,
entry preface, portrait/box visibility and ordered background fades.
The original five-atlas inventory package now registers with bounded native
resource ownership; shared case-1 cursor/layout/capacity setup also runs.
Original sprite dispatch and opening-display transitions now run through a
Quest-tested GPU transport. Original 32-bit scalar sorting and dynamic item
icon/quantity/equipment construction now run with bounded table publication.
Cursor highlighting/placement and original disc-backed item names now run
with a separate font frame. Original cursor/top-menu navigation is now
device-verified. The original entry preface, portrait/box visibility and
background RGB fades now run through checked native stages. Complete screen
composition, actions and gameplay return remain pending. The live inventory button stays
gated; installed v0.86 is unchanged.

See the [full port plan](PORT_PLAN.md) for milestones and acceptance checks.

This work uses original engine components from `port/`. The DMA walker and VU
core are shared with the upstream branch. OpenXR handles the virtual room,
head tracking and Touch input around the game's original camera view.

## Build and verify

Use the Android NDK (r27d tested), CMake and Ninja. From the workspace root:

```powershell
cmake -S arm64 -B arm64/build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=<NDK>/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
cmake --build arm64/build
& arm64/Test-OnQuest.ps1 -Adb '<path to adb.exe>' -Serial '<Quest serial>'
```

The default device checks do not need an ISO. No check installs or modifies
an app. The NDK cross-build and ELF inspection passed (ELF64 / AArch64 /
Android dynamic linker). The connected Quest 3 passed the EE/DMA checks
and all 100,000 VU comparisons, with zero mismatches. With the optional
fixtures, it also passed the opening asset and expanded room checks.

## Quest virtual-room host

After configuring the NDK build, use the Android SDK build tools, a Java
runtime and the Android debug keystore to package and verify the app.

```powershell
& arm64/Build-Apk.ps1
adb install -r arm64/build/recvx-arm64-host.apk
adb shell am start -n org.recvx.arm64/android.app.NativeActivity
adb logcat -d -s RECVX-ARM64:I
```

The packager accepts explicit `-Sdk`, `-Java` and `-Keystore` paths.
The APK carries only the ARM64 host library and Android metadata. The host
uses the same EGL setup as the offscreen regression suite. Quest verified
title presentation in a centered 1064Ã—798 viewport in its 1280Ã—800 window
and redrawing after EGL context recreation.

The host looks for a single supported `.iso` in
`/sdcard/Android/obb/org.recvx.arm64/`. Use a PS2 NTSC-U Code: Veronica X
image with disc ID SLUS-20184. If the image is on the PC, install the app
first, then copy the ISO separately.

```powershell
adb push '<path to SLUS-20184 ISO>' /sdcard/Android/obb/org.recvx.arm64/game.iso
```

The loader reads `SYSTEM.CNF`, validates the original MIPS ELF and its
loadable segments, and verifies the headers of `SYSTEM.AFS` and
`RDX_LNK.AFS`. It keeps the ISO open read-only for subsequent reads.
The host calls the original `InitGdSystemEx(256)` to build the engine's file
directory. Its work buffers use the original first-fit heap with native
links and the original 64-byte block alignment. The original GDFS calls
read `DISCID.BIN` and the first sector of
`RM_0000.RDX` from `RDX_LNK.AFS`. Startup logs report the result. A failed
mount or directory initialization is retried on resume.

The original ADXF wrapper mounts `SYSTEM.AFS`, `ADV.AFS` and
`RDX_LNK.AFS`. Startup reads the font bank (`SYSTEM.AFS` member 1),
warning package (`ADV.AFS` member 0), title package (member 2), and Options
package (member 4). The host uses the original title and menu drawing routines with a GLES
textured-quad backend. After the opening movie, it opens at Press Start. Start or A advances,
the D-pad or left stick moves the selection, and B goes back. A keyboard can
use Enter, the arrows and Escape. Touch A or the left menu button skips the
opening movie and advances Press Start. The title prompt uses the original
128â€“255 brightness cycle. Head movement updates the stereo view of the room
while the game screen stays in place. Game logic runs at 30 Hz, movies at 60 Hz, and
OpenXR presents the surrounding room at the headset's display rate.
The host feeds the original pad reader at
30 Hz. Options uses the original menu handlers for sound mode, Key Config,
Screen Setup, Monitor Adjustment and Default. Exiting Options writes the
selected sound mode, key type and X/Y position to app-private storage.
The display offset applies immediately. Native game audio and vibration
remain pending. Confirming a difficulty runs the original `bhFirstGameStart`,
reads and expands `RM_0000.RDX`, stages it through `bhSetRDT`, and draws the
starting camera cut. Claire's default body, textures, movement clips and
per-frame travel distances come from `SYSTEM.AFS`. The opening scripts,
warning sequence and Load Game remain pending.

### Character input

In the starting room, Android gamepad input goes through the original
`bhSysCallPad` and `bhControlPlayerPad` routines. Key Config selects the
original Type A, B or C action table. Use the D-pad or left stick to move
forward, back up and turn. Hold the layout's Run button while moving forward
to run. The player blends the original locomotion clips, uses the original
wall collision routine and follows the room's authored camera cuts. The
full player task, including combat and interactions, remains pending.

Claire's complete 100-slot motion bank is bound as native `MN_WORK`, with
60 populated body and accessory clips. Per-frame animation and blending run
through the original `bhSetMotion`. Quick-turn shares the original reverse
state transition, eight-frame rotation, and lateral movement code. Hold back
and tap the configured Run button for a 180-degree turn. With default Type A
Touch controls, hold the left stick down and tap B.

The equipped-hand loader decodes both original weapon models, their textures
and the player motion suffix beginning at slot 100. New Game loads the
unarmed package. The original `bhSetWeapon`, `bhObjWpn` and `bhCalcModel`
attach its hand models to Claire's wrist bones. Equipped weapon packages and
their motions are available to the remaining combat and equipment runtime.
Attacking, damage and campaign equipment changes still need integration.

### Original event execution

The ARM64 host links the original `event.c` scheduler and handlers for task
creation/cancellation, frame scripts, Sleep/Sleeping, For/Next, both While
endings, conditionals, flags, supported state comparisons/stores, pad checks
and the shared event timer. Actor commands now use the original visibility,
node display, object/item joint attachment, work selection and motion-pause
handlers. Wall, event and floor collision activation also uses the original
handlers. Their adapters check actor slots, joint indices, room motions and
multipart companion pointers before dispatch. Effects work selection remains
unsupported. Native bytecode and saved continuations use full
host pointers. Dispatch checks instruction ranges, operands, loop work bounds
and a per-frame instruction limit.

Room startup checks all scenario entries before allowing any script to alter
game state. `EffDispSet` (`0x43`) and `EffAmbSet` (`0x44`) now call the original
handlers with bounded record/map and ambient-bank checks. RM_0000's 14 authored
effect records now pass runtime readiness checks and are published to the live
pool. The complete room scenario still stops during preflight at `MaskSet`
(`0x3c`), byte 1746. The full opening sequence does not run yet. The current movement view remains
available while the remaining actor, camera and presentation commands are
ported. Unsupported commands report a dependency instead of advancing as
no-ops. Event work is cleared before room backing is released.

With `-FixtureDirectory`, `Test-OnQuest.ps1` also runs `recvx_check_events`.
Quest checks cover concurrent timing, branch/loop behavior, pad conditions,
state stores/comparisons, cancellation and malformed continuations above
4 GiB. Actor checks include native attachment pointers and signed offsets,
all four actor visibility selectors, task work, multipart motion pause/resume,
and invalid actor/joint/collision references. Quest GLES checks against the
live RM_0000 verify that hiding Claire and her shadow removes 11,204 pixels and hiding her
body mesh removes 9,831 pixels, with exact frame restoration after showing
them again (animated lighting held fixed). The ordinary player task respects
the original motion-enable and hidden-actor flags. RM_0000 event 38 executes
all 44 original flag commands and matches an
independent disc-derived reference. This isolated event check does not establish
full opening-script or campaign compatibility.

### Original shadows

The common effect/font bank from `SYSTEM.AFS` member 1 supplies four native
model records at their original effect IDs (0, 1, 7 and 13). The shadow uses
ID 1's two modifier meshes: 51 vertices and 94 triangles. The bounded decoder
owns its source and rejects malformed chunk sizes, indices and nonfinite
positions before publishing the bank.

The host calls the original `bhSetShadow` and `bhEff001` for Claire's shadow.
Room actor joint work is allocated through the original `bhKeepObjWork` with
arena bounds checks and initialized through `bhCalcTree`. The shadow update
adapts the current local joint matrices to the original world attachment,
uses the original ground query, and preserves hidden/inactive actor behavior.
The renderer draws the original volume's front and back faces into a depth-
tested mask and applies the original fog-dependent GS subtraction equation.
It handles the theater's D24 and the offscreen surface's packed D24/S8 depth.
GPU checks match the GS equation on 3,916 pixels and verify depth cancellation,
near-plane rejection and framebuffer restoration.

`Test-OnQuest.ps1 -FixtureDirectory ...` runs the shadow lifecycle/GPU checks
and `recvx_check_effects` with the generated `common-effects.bin` fixture.
General effects and joint-list-driven shadow sizing remain pending; active
unsupported effects report an error. This backend does not enable the full
opening until its other commands are ported.

### Original room effect tables

The host prepares all 14 RM_0000 effect records through the original
`bhSetEffectTable`, `bhSetEffectTb` and `bhSetEffectLink`. Preparation restores
the live pool, signed 16-bit mapping, room records and actor pointers after
capturing owned native work. Actor-local joint matrices are temporarily viewed
in world space for the original linker. It preserves the authored work position
until the first update, the linker's baked record position, fallback-to-zero
indices and inactive-parent behavior.

Publication checks room identity, unchanged records and free slots before
writing anything. Work-buffer self pointers are rebound to their final native
addresses, so destroying the prepared table cannot invalidate live effects.
The original hide/show handler uses the published record-to-slot mapping.
Missing, negative and out-of-range mappings fail before script execution.

Quest checks cover all four actor link selectors, high native pointers,
first-fit allocation, scalar handoff from all 14 disc records, original display
and ambient handlers, atomic publication and stale/full-pool rejection.
Effect-to-effect links and linked/frozen actor transforms remain explicit
dependencies. All 14 starting-room records now publish after their common and
room-specific textures are registered. The metadata-only preparation check
intentionally has no texture heap and still stops at record 0's missing atlas.
The original flames and camera-selected filter can now render in the movement
view; the incomplete room scenario is not partially executed.

The native runtime now calls the original `bhEff115` type-2 handler and its
`bhEff124` child, preserving cue consumption, same-frame initialization, authored
UV phases, gravity, ground contact and expiry. Other 115 variants remain gated.
The GLES adapter draws the original effect-33 atlas with the original billboard
transform, color conversion, fog and additive blend. Original shared palette
uploads preserve all 64 bank tiles; palette changes invalidate the GPU atlas cache.
`njUnitRotPortion` now preserves native matrix pointers above 4 GiB.

Quest checks compare all 1,024 shared palette colors, the entire particle lifecycle,
pause/loading/full-pool behavior, malformed animation state, depth rejection and
GPU palette-cache restoration. Across 34,596 sampled GPU color channels, the
original atlas draw differs from an independent scalar GS calculation by at most
2/255. The existing player, actor visibility, shadow and engine checks also pass.

The original effect-9/type-1 handler now drives the opening room's two flames,
including prop 6's joint-2 attachment. The native runtime composes actor-local
joint work with the actor transform before the original update. It preserves the
authored eight-frame loop, pause/hide behavior, inactive-parent behavior and
source-alpha GS blend. Other effect-9 variants remain explicitly gated.

New Game now registers all 40 common effect texture entries from the original
SYSTEM.AFS member 1 before appending room-specific atlases. The bounded package
loader owns the PVP data and checks packet spans, palette capacity, supported
image types and the expanded entry count. Texture indices and palette banks use
the original `ef_info` table and `bhSetMemPvpTexture` registration path.

Quest checks cover both real flame records, moving joint attachments, invalid
parent/joint/UV references, the full 512-entry queue, all eight UV cells and the
27-packet/40-entry common texture expansion. Across 92,256 GPU color channels,
the flame matches an independent scalar GS blend calculation within 2/255.
Linked and unlinked flames at the same world position produce identical pixels.
Existing particle, movement, quick-turn, actor-visibility and shadow checks pass.

The runtime also calls the original `bhEff125` emitter and `bhEff126` child
handlers. All seven child types and four opacity versions are supported: their
three effect-35 texture pages, authored UV tables, initial scales, random offsets,
falling/upward/stationary motion, ground rejection and sentinel expiry remain
original. Linked emitters use the same guarded joint-to-world adaptation as flames.
Unsupported cues, type changes that would escape an initialized UV table, invalid
native pointers and absent texture pages stop before the original handler reads them.

Quest tests cover all three actual opening emitters, including item 5's attachment,
all 28 type/opacity combinations, pause/loading gates, cue consumption with a full
pool, and delayed initialization when a child occupies an earlier work slot. Across
968,688 GPU channel samples, texture-page selection, both quad UV orientations and
GS alpha blending match an independent scalar calculation within 2/255. The
existing particle/flame, engine, player, shadow and actor-visibility checks pass.
This does not enable a partial opening sequence or force progression flags.

The original `bhEff2D` handler now updates opening effect 20/type 45, including
all three texture pages, cached global texture identity, visibility cues, UV crops,
mirrored scale and reciprocal depth. The GLES screen path preserves the original
640x480 coordinates, GS 12.4 vertex quantization, Ninja depth-clamp writeback,
color conversion, point/bilinear filtering and stable ordering-table buckets.
Overlays depth-test without writing depth and preserve framebuffer alpha.
Other screen-effect types and alternate texture banks remain gated.

Quest tests cover all three actual overlay records and the 128-entry queue,
including inactive trailing records and rejected overflow, stale texture identity,
invalid native work and nonfinite or unsafe coordinates. Across 953,796 sampled
GPU color channels, pages, crops, filtering and a doubled viewport match an
independent scalar GS calculation within 2/255. Exact nearest-texel interpolation
ties are excluded from that comparison; bilinear sampling covers every interior
sample. Separate checks verify GS layer ordering, stable equal-depth insertion,
depth rejection/no writes, hidden overlays and return to unchanged 3D rendering.
The full opening remains gated at scenario opcode `0x3c`; progression flags are not forced.

Effect 21 now calls the original `bhEff021` update and `bhDraw021` cinema
callback. The original `njDrawPolygon2D` packs the four black-bar/gradient
polygons into a bounded native transport. ARM64 uses native pointers for the
callback queue and copies the original packed coordinates, raw colors and priority
before the callback's stack storage expires. Original blend lookup and GS register
updates are retained; unsupported callbacks and primitive variants fail explicitly.

Quest checks cover the real opening record, cue/camera flags, paused and hidden
task behavior, rejected callback pointers and 512 callbacks producing 2,048 owned
polygons. Across all 4,608,000 color channels at 640x480 and 1280x960, cinema bars,
gradient alpha and viewport clipping match an independent GS blend calculation
within 1/255. Additional checks verify depth rejection/no writes and exact return
to textured 3D and screen-overlay rendering. Native blend lookup bounds are
checked before table access. All authored opening effect records now pass their
readiness checks; the incomplete room scenario is still not executed.

The camera filter calls original `bhEffFil` for effect 92. Its activation/selector
checks, four UV orientations, 16 opacity settings, latched parameters and
pause/hide behavior are preserved. The shared screen renderer draws its original
texture and cached global identity with the original alpha conversion.

Quest tests publish and step the actual 14-record effect table with native links,
verify both flames and the camera filter queues, and check all filter states.
Across 17,510,400 GPU color channels, four orientations, four representative
opacity levels and a doubled viewport differ from the scalar GS reference by at
most 3/255. The 8,486 samples beyond 2/255 also pass explicit one-unit texture
interpolation plus framebuffer blend-rounding bounds. Existing engine, player,
shadow and actor-visibility regressions pass with live room effects. The next
scenario dependency is `MaskSet` (`0x3c`) at byte 1746; preflight leaves the
opening's progression flags unchanged.

The original `bhCommonCtr`/`Common_controll` path now handles subcommands
2, 3, 5, 6, 7, 9, 11, 12, 13, 23, 36 and 41, plus original no-op 0. These
preserve transform steps/signs, unsigned position words, angle conversion and
32-bit wrapping, absolute position backup/floor updates, selected-node rotation,
active-model root reset and the original flag masks. The original case bodies
remain shared with the desktop source. Other common-control branches are excluded
from the native dispatch until ported; unsupported subcommands are reported.

Preflight uses each admitted subcommand's actual length (including odd-length
commands), rejects truncation and crossing into another script entry, and leaves
room/task state intact on failure. Runtime dispatch checks native task, actor,
model and node references before calling the original handler. Quest checks cover
concurrent suspended-task state, addresses above 4 GiB, signed wraparound and
actual sign/rotation/position bytes from both original opening-entry branches.
The full native/GPU/player/actor/shadow regressions pass. Complete opening
preflight now stops at `MaskSet` (`0x3c`), byte 1746; the scenario is not partially
executed and its progression flags remain unchanged.

`LightSet` (`0x35`) now calls original `bhLightSet` with bank/index guards. It
preserves the original zero/nonzero behavior for enabling lights and selecting
the room or event bank, changing only the enabled bit. Quest checks exercise
both native banks above 4 GiB, complete record/canary preservation, absent and
out-of-range banks, truncated commands, and actual light-command bytes from
both opening-entry branches.

In the real New Game room, disabling point light 11 removes it from the original
light controller and changes 105,892 GPU pixels. Re-enabling it restores the exact
frame with animation held constant for comparison. Full native/GPU/player/actor
and shadow regressions pass. Opening scenario preflight now reaches `MaskSet`
(`0x3c`) at byte 1746 without executing the partial scenario or changing its
progression flags.

Original `bhPlItemCheck` (0x11), `bhItmSetCk` (0x23) and `bhItmCk` (0x0e)
now dispatch without replacement game logic. Inventory lookup preserves the original
slots 1..11 within the active 16-slot bank, low-byte item comparison and extended
item-72 room flag. Item-state handlers retain the original pickup-bit masks,
interaction-index early return, take/cancel flags, item visibility and collision
activation. Native guards bound inventory storage, item work, trigger records and
pickup flag arrays before dispatch.

Quest tests cover every storage bank and slot, all 512 pickup bits, both condition
branches, collected/uncollected and accepted/cancelled paths, unrelated state,
invalid references and truncated commands. Tests also execute the actual inventory
conditions and all six pickup-state commands from RM_0000. Existing engine/GPU,
60-motion player, quick-turn, actor, light and shadow checks pass. Complete room
preflight reaches `MaskSet` (0x3c), byte 1746, with progression unchanged.
This ports script-side item state; the original inventory interface and pickup
interaction flow still require integration.

Original `bhArmsItemCheck` (0x26) and `bhSubMapBusyCk` (0x81) now run
through the bounded native event dispatcher. Equipment checks compare the full
signed weapon value with the unsigned script byte; absent native player work
is rejected. Map/menu checks retain both original busy bits (0x8 and 0x40000)
and zero/nonzero operand behavior. Neither check changes equipment or menu state.

Quest tests cover every operand byte, signed weapon extremes, all combinations
of busy bits, unrelated flags, both conditional branches, missing player work
and truncated commands. Actual RM_0000 bytes select the original wall-collision
branches for weapon 2 and the map/menu condition. Existing native/GPU/player,
actor, lighting and shadow tests pass. Complete opening preflight now stops at
`MaskSet` (0x3c), byte 1746, without changing progression flags. Campaign event-audio
playback is still pending; map/menu status checks do not establish a working map UI.

Original `bhSeOff` (0x18), `StopNativeEventSe`, `StopMidi`, `sdMidiStop`
and `SdrBgmStop` now execute through their original bodies. Native initialization
preserves pointer-sized handle indirection, original event-slot allocation and
its mapping to MIDI ports. A bounded transport drains the original 128-entry
queue into the existing PC TSNDDRV synthesizer. Its SQ sequencer, SPU2 ADPCM,
ADSR, mixing and reverb code are shared; Android provides a 48 kHz stereo PCM
callback through AAudio. The PC synthesizer is the project's existing hardware
adaptation, rather than original PS2 IOP driver source.

Quest checks use an independently constructed HD/SQ/ADPCM fixture to start two
audible PCM sources, execute the original opening's SeOff bytes, release only
the selected source and verify the other remains. They cover all five event
slots, original out-of-allocation behavior, inactive slots, invalid native
handles/ports, queue exhaustion/wrap, rejected commands, truncation, restart and
real AAudio callback execution. Full native/GPU/player/actor/light/shadow tests
pass. Opening preflight advances to `MaskSet` (0x3c), byte 1746, with
progression unchanged. Campaign sound-bank loading, playback requests, other
sound commands and their frame timing remain pending. This does not yet make
campaign effects or music audible.

Original `bhSetItmMotion` (0x9c) now calls original `bhSetMotion` and
`bhCalcModel` for admitted item work. The native path preserves the original
running-mode/item-task gates, fast-track key evaluation, attributes, signed
frame advancement/wrapping and root/child world matrices. Compile-time checks
verify the shared O_WRK/BH_PWORK motion prefix. Runtime guards bound item slots,
room-motion bindings, frames, steps, model nodes, mapped work and native matrices.
Interpolation, remapping, linked bases and non-fast motion formats remain
explicitly gated; this does not establish full item-control compatibility.

Quest checks cover addresses above 4 GiB, independent root/child translation
references, forward/reverse wraps, stationary frames, all 256 item modes,
disabled tasks and rejected invalid bindings/formats/truncation. Actual RM_0000
commands for items 1 and 2 advance independent active fixture state and preserve
dormant room work. Existing native/GPU/player/actor/light/shadow/audio tests pass.
Complete scenario preflight reaches `MaskSet` (0x3c), byte 1746, without
executing the partial opening or changing its progression flags.

Original `bhArmsItemChange` (0x27) now dispatches with bounded player-bank
and package selectors. It preserves the original right-weapon/save-bank update
and complete four-byte monitor request; the left-weapon field stays unchanged.
The native resource adapter replaces Claire's 21 supported hand/weapon packages,
registers textures in the original release/reload order, initializes reserved
objects through `bhSetWeapon`, and rebinds motion slots 100..199. Base and event
motion ranges retain their owners. Player locomotion clips and translation data
are selected from the original equipment/health tables, with validation before
publication. Staged package/cache allocation precedes live replacement; failed
texture registration attempts restore the previous registration.

Quest checks cover all 21 selectors and four saved player banks in the original
handler, rejected state/bytecode, all 21 Claire packages and 163 weapon motions,
GLES hand rendering/wrist sockets, both locomotion rows and all three health
levels, and invalid clip/selector preservation. Repeated swaps reproduce each
weapon's initial GPU output; returning to unarmed restores the original complete
room/player frame exactly with animation held fixed. Existing native/GPU/player,
actor/light/shadow/audio regressions pass. Complete preflight advances to
`MaskSet` (0x3c), byte 1746, with progression unchanged.

This ports equipment state and native model/motion resource replacement. The
original monitor request remains pending: arms sound-bank loading and transfer
acknowledgement, other characters/costumes, and full combat are not established.
The standalone integration check is built with `arm64/Build-EquipmentCheck.ps1`;
push `arm64/build/check-equipment-runtime-arm64` to the Quest, make it executable,
and run it with `/sdcard/Android/obb/org.recvx.arm64` as its argument.

Original `bhPlItemLost` (0x31), `bhEtcAtariSet2` (0x25) and
`bhDelYakkyou`/`bhDeleteYakkyou` (0x6b) now dispatch through their unchanged
original bodies. Native guards bound the inventory bank and interaction table.
Item loss searches slots 2..11, removes the first matching item byte, preserves
the original absolute-slot comparison against selection metadata, and shifts
through slot 12 without clearing that source slot. Interaction updates replace
unsigned attributes, parameters and type while retaining flags and geometry.
Case cleanup clears the complete flag word only for active effects with ID 0
or 7, preserving the remaining effect work.

Quest tests cover all storage banks/slots, all item bytes, metadata edge values,
duplicates, excluded slots and slot-12 copying; all 256 interaction indices with
record/canary preservation; and the full 512-slot effect pool with active-bit
and ID variations. Actual RM_0000 commands, invalid banks, ignored padding and
truncated/cross-entry input are checked. Existing native/GPU/player/actor/light,
shadow/audio/equipment regressions pass. Complete preflight advances to
`MaskSet` (0x3c), byte 1746, without changing progression flags. These handlers
do not complete the inventory UI, item-use flow, trigger-event runtime, or
spent-case spawning/rendering.

Original `bhRandamSet` (0x63) and `bhRandTest` (0x8c) now dispatch with
a native retail-compatible random generator. The generator's 32-bit wrapping
multiply/add, 31-bit result, raw seed handling and initial state 1 are transcribed
from `rand`/`srand` in the user's SLUS_201.84 executable at 0x125320/0x125310.
All native game translation units share this generator, including effect calls;
the desktop generator is unchanged. Original handler bodies preserve float
rounding (including the value 100), RandTest's signed-zero/raw float bits, seed
word order and task-zero register writes even when another task is executing.
A zero RandamSet modulus is rejected before execution.

`random_reference.py` executes the original EE RNG instructions to generate
private reference vectors, including zero/high-bit seeds and float-boundary
cases. `opening_fixture.py` now generates `random.expected`; `Test-OnQuest.ps1`
passes it to the event check. Existing fixtures can add it with:

```powershell
python arm64/random_reference.py --iso '<path to SLUS-20184 ISO>' --output arm64/build/fixture/random.expected
```

Quest tests verify the generator against those vectors, every nonzero modulus,
zero/truncated rejection without RNG consumption, both RandTest seed modes,
shared task-zero/CmpB behavior, concurrent task ordering and actual RM_0000 bytes.
Full native/GPU/player/actor/light/shadow/audio/equipment regressions pass with
the unified generator. Complete preflight now reaches `MaskSet` (0x3c), byte 1746,
with progression unchanged. Generator/handler parity does not establish the
complete campaign's frame-by-frame RNG call ordering; the full opening and
remaining gameplay/audio runtime are still incomplete.

Original deferred music/ambience commands BgmOff2 (0x93), BgmOn2 (0x95),
BgmOn2Ex (0xa7), BGVoiceOn2 (0x94) and BGSeOff2 (0x92) now dispatch through their
original bodies. These commands update requested IDs, volumes and room-transition
flags; they do not immediately stop, start or fade playback. Native sound startup
shares the original metadata-only initialization block. The non-native original
initializer keeps its previous behavior. Background request slots are bounded
to their two original records.

Original CineSet (0x12) and EventSkipSet (0x6a) now retain cinema/control/task
flags, offset retention/reset and zero/nonzero skip-policy behavior. Quest tests
cover all selector/volume bytes, music request precedence, both ambience slots,
unsigned sound IDs, unrelated state, padding, initialization and malformed input.
Deferred stop requests leave running synthesized PCM unchanged. Cinematic flag
checks cover all selector bytes, and the actual RM_0000 CineSet command blocks
native movement until the original cinematic exit restores it. Existing native,
GPU, RNG, player, actor/light/shadow, audio and equipment checks pass.

Complete preflight now reaches MaskSet (0x3c), byte 1746, with progression
unchanged. Request production is ported; room-transition sound consumers,
background/music playback and fades remain pending. Cinema/skip flags do not
establish camera scripts, face animation, input-driven cutscene skipping or full
opening compatibility.

Original PuruPuruFlagSet (0x9e), FaceRep (0x9a), FacePauseSet (0x8f),
FaceReSet (0x90), MaskStart (0x3e) and LipStart (0x3f) now dispatch through
their original flag-setting bodies. Native sound initialization restores the
original vibration-policy default. This ports policy state, not Touch vibration
output.

The room owns a cleared 1,092-byte scalar face-control allocation for the original
actor family (ID >90). Native publication registers actor identity and the full
pointer, and facial commands require a matching active RDT mapping and registered
work. Teardown clears bindings before the room releases storage. The original
packed pointer words remain unused: actor MASK_WORK/LIP_WORK context publication,
actor expression/lip binding, deformation and animated rendering are still gated
at MaskSet. Scalar staging does not claim a ready facial-animation context.

Quest checks cover 128 reversed record-to-work mappings, all action bytes,
flag masks, full target/canary preservation, unrelated actors, stale IDs,
foreign work pointers, missing/invalid banks, truncation and teardown. Actual
RM_0000 FaceRep bytes update owned addresses above 4 GiB without changing geometry
while the facial context remains unready. Native room loading checks cleared
allocations, and existing engine/GPU/RNG/player/actor/light/shadow/audio/equipment
regressions pass. Complete opening preflight reaches MaskSet (0x3c), byte 1746,
with progression unchanged.

`face_sampling.c` now validates the room's size-prefixed expression and lip
banks before publishing their pointers. Typed, immutable views retain full native
addresses. Sampling calls the original `face_bh.c` parameter/frame/interpolation
and lip-sync bodies. Native branches also retain the deformation bodies described
below; legacy pointer fixups remain excluded. The original integer-angle lip fields, six phonemes,
two-frame tick, easing and muscle mask are preserved. Bad extents, missing
terminators, nonfinite expression values, unordered frame keys, zero durations,
invalid phonemes and foreign cursors fail without committing outputs.

Quest checks compare all 20 RM_0000 expressions (4,410 frame samples) and 19 lip
sequences (1,831 ticks) with independent scalar references. Synthetic tests cover
all easing branches, actor binding, preserved eye/unselected muscle parameters,
integer-angle conversion, malformed data and high pointers. Cosine easing uses
the original initialized sine lookup, including its mirrored-table rounding.
These are sampling foundations: live actor control and rendering remain
unconnected. MaskSet remains blocked at byte 1746.

`face_mesh.c` stages a detached native facial mesh for each available MASK.
It validates descriptor counts, muscle indices/scales, adjacency indices, jaw
pivots/weights, parameter extents and the matching type-51 vertex packet. It
owns separate MASK, bind-pose and output buffers. A separate array holds full
native neighbor pointers; the encoded MASK indices stay immutable and the old
32-bit pointer slots are never reused. Vertex copies use the measured packet
extent and terminator instead of the original fixed 128-word overread tail.

The original `_fmCnkCalcMuscle` C control loop now compiles for ARM64. Native
branches replace its EE quadword copy and reverse accumulation instructions,
with separate multiply/add operations and portable EE float handling. Quest
checks exercise both opening actors (1,571 and 1,486 vertices), their 16,426
neighbor references, all 32 muscles, base keys and 20 expression clips. Across
1,064,601 position channels the maximum error against an independent double
reference is 0.000000239 model units. Header, normal, flag and weight lanes stay
unchanged; zero-expression round trips restore exact bind-pose bytes. Invalid
counts/indices/connections, truncated buffers and nonfinite/overflowing samples
reject before publication or output changes. Full engine/GPU/player/actor and
equipment regressions pass with these allocations staged.

This workspace remains detached: actor exp1/exp2 and the live model are not
replaced. Per-frame actor control and skin/render integration must be completed
before MaskSet can execute.
Whole-room opening preflight still rejects opcode 0x3c at byte 1746.

The original `_fmCnkSetJaw` and `_fmCnkCalcJaw` now run on ARM64. Room staging
initializes the detached jaw matrices after the original trig/VU setup. Explicit
matrix setup avoids the redundant global stack push/pop; the original pivot
math, signed angle conversion and full-/partial-weight control bodies remain.
Native branches execute the original VU transform operations using native
addresses. `njCopyMatrix` also uses a bounded native copy instead of truncating
addresses. Vertex stores preserve the skin packet's fourth lane: the original
jaw assembly initializes only xyz of its temporary point, so copying its
uninitialized fourth lane would corrupt metadata.

`rx_face_mesh_deform` applies a fresh muscle pose followed by the original jaw
transform, with readiness, finite-value and integer-angle-range checks before
writing output. Quest checks cover both opening actors, full and partial weights,
20 expression clips, original phoneme conversion for every base-parameter group,
positive/negative jaw motion and translation, five rotated pivot axes, repeated
updates, high-address overlapping matrix copies and degenerate/unready/invalid
samples. Across 1,207,644 position channels, the maximum independent matrix/
vertex-reference errors are 5.2277177e-8 / 1.20140612e-7. Metadata stays intact
and repeated evaluations are byte-identical. Native/GPU/player/actor/equipment
regressions pass; complete opening preflight remains at MaskSet, byte 1746.
Jaw deformation is tested in detached work and has not been attached to live
actors. Per-frame actor control and skin/render integration remain.

Native auxiliary staging now copies each facial bind hierarchy and validates
the original face/tongue/tooth/eye mesh ordinals. The original tooth and eye
insertion bodies run on that private hierarchy. The original tongue insertion
body uses validated owned source/output packets instead of its PS2 allocator
and fixed-tail copy. Both opening tongues have bounded 804-byte type-51 packets.
All original nodes and four inserted nodes remain reachable exactly once;
the live actor hierarchy and its models are unchanged.

Eye bind matrices use the original translate/rotate/scale order with native
parent references and explicit matrices, preserving the caller's matrix stack.
The original `_fmCnkCalcTang` and `_fmCnkCalcEye` bodies run through native VU
instruction branches. `rx_face_mesh_animate` evaluates muscle, jaw, tongue and
eye in original order on detached work. It validates tongue-angle ranges,
finite inputs and defined eye targets before any output changes. XYZ stores
preserve all skin metadata/normal/weight lanes.

Quest tests cover 300 facial frames, 20,700 tongue position channels, independent
eye bind matrices and exact eye-angle references. Maximum tongue position error
is 5.96046448e-8. Cases include all opening expression clips, original lip
conversion across all parameter groups, signed rotations, tongue Z scaling,
front/back/zero-Z gaze, reversed eye orientation, exact repeated frames and
jaw-driven tooth/tongue parents. Invalid samples and malformed auxiliary room
data preserve output and the active room. Existing native/GPU/player/actor/
audio/equipment checks pass. Actor exp1/exp2 and per-frame
skin/render publication remain gated; whole opening preflight still stops at
MaskSet (0x3c), byte 1746, with progression unchanged.

`face_pose.c` now stages the original 31-node facial motion view. The original
`_fmSetObjP` traversal/copy loop is retained through a native entry point with
owned object/matrix allocations. Nodes 6..14 have the original motion-protection
flag, leaving 22 body tracks. The original bhControlMask tooth/tongue/eye angle
copies feed that flat view before the original `bhSetMotion` and `bhCalcTree`.
This preserves fast unsigned angles, signed short-angle interpolation, frame
attributes and forward/backward wrapping. Hokan, mirror and custom remapping
remain outside this bounded entry point.

The native adaptation of `npCalcSkinFM` uses the original skin group, ten blend
levels, four-node joint offset above joint 10, partial inverse-rotation matrices
and portable VU transforms/blending. The opening actors use group 5. Face/jaw
bind output stays separate from the posed vertex packet, preserving normals'
and positions' metadata lanes and preventing repeated-frame accumulation.
The staged flat view borrows immutable model/texture data for the room lifetime.

Quest tests cover eight opening clips plus normal-format fixtures: 100 pose
cases / 917,100 position-normal channels across both actors. Independent motion,
hierarchy and double-precision blend references give maximum matrix/skin errors
of 7.19405967e-6 / 8.37739981e-6. Protected facial transforms, all ten weights,
shifted joint indices, fractional/last frames, attributes, exact repeats and
caller bank/stack preservation pass. Invalid slots/frames/steps/keys, altered
protection flags, insufficient matrix space and malformed skins reject before
face/pose changes. Existing engine/GPU/player/actor/audio/equipment checks pass.

These remain staged views. Live actor exp1/exp2 control, native replacement of
face.c's packed pointer fields and per-frame actor publication must be connected before original MaskSet can execute. Opening preflight
still rejects 0x3c at byte 1746; no campaign progression is forced.

The renderer now accepts independently bounded vertex and polygon packets.
`rx_draw_chunk_parts` validates the complete batch before submission, including
stream extents/terminators, strip indices, finite vertices/matrices and texture
selectors. Validation does not run lighting or change materials. Actual draws
use the original Ninja model entry points and existing lighting, clipping and
GLES path. One ordering table covers every model in the batch, with stable
ordering inside equal buckets. Legacy contiguous model drawing keeps its
existing range checks and rendering behavior.

`rx_face_pose_draw` submits six visible original character models. Skinned body
vertices use the actor/view transform once; eyes, teeth and tongue use their
original joint matrices. Exact owned body/tongue ranges coexist with immutable
MDL polygon/model data. Unsampled poses are rejected, and draw calls preserve
the caller's matrix. Sink failures retire draw work and allow an exact retry.

Headless Quest GPU checks render both opening actors with their original
registered textures. Their 562/549 packets cover 4,512/4,905 pixels. Split-buffer
frames match old contiguous-model draws plus an independent ordering-table
reference byte for byte. Animated diagnostic poses remain visible and change
4,459/4,807 pixels. Separate translucent models pass independent near/far and
equal-depth blend readbacks. Truncated/foreign/overflowing spans, invalid final
model matrices/commands/indices and missing terminators reject before any packet
is submitted. Existing native/GPU/player/actor/audio/equipment regressions pass.
These diagnostic frames do not enable the original scenario: actor exp1/exp2
control, packed facial pointer migration and live publication are still gated.
Opening preflight remains at MaskSet (0x3c), byte 1746.

Touch controls feed the original pad reader directly. The left stick selects
movement and menu directions. A/B/X/Y map to the gamepad face buttons. The
left menu button maps to Start. Grips map to L1/R1, triggers to L2/R2, and
stick clicks to L3/R3. With default Type A controls, hold B while moving
forward to run. The headset's system menu handles leaving the application
and recentering the view.

## Original movies

The startup movie is `MOVIE/MV_016.PSS`, as selected by the original
`Adv_CapcomLogo` task. Start skips it with a fade. The Android
host pauses movie audio and video when its window loses focus and resumes
from the same position. The player streams the ISO directly. It demultiplexes
PSS, decodes MPEG-2 and stereo PCM, and uses the AAudio output clock to select
GLES frames at the authored cadence and display size.

The original `PlayStartMovieEx` entry point accepts the other numbered
movies and preserves their display and skip settings. Automatic playback
from campaign events depends on the scenario runtime.

## Original opening asset check

Generate a private fixture from the supported disc image. This uses the
upstream ISO/AFS/RDX reader to extract RM_0000's root MDL and the first
populated slot in its room motion bank. The fixture and its reference stay in
the ignored build directory; they are not compiled into the executables.

```powershell
python arm64/opening_fixture.py '<path to SLUS-20184 ISO>'
# In a Visual Studio x86 Developer PowerShell, with the Windows SDK available:
& arm64/Test-OpeningReference.ps1 -Clang '<path to clang.exe>'
& arm64/Test-OnQuest.ps1 -Adb '<path to adb.exe>' -Serial '<Quest serial>' -FixtureDirectory arm64/build/fixture
```

`ninja_binary.c` translates chunk MDL and two-track MTN records to native work
tables without writing to the source bytes or storing host pointers in disc data.
The 32-bit Windows oracle builds the original `binfunc.c` and compares node
hierarchy, transforms, texture references, chunk references and motion tracks.
The standalone ARM64 check uses the independent Python transcript to check those
decoded records and sampled keys. Desktop and Quest checks passed on the real fixture:
67 nodes, 67 chunks, 32 textures and 66 samples from a 1,212-frame room motion.
The harness also exercises invalid ranges, cycles and failure without replacing
existing work records. The decoder accepts the ordered hierarchy and track format
used by this fixture; it does not yet validate the full polygon/vertex streams,
implement skinning or replace the engine loader. Both original source buffers
were above 4 GiB during the Quest run.

`room_binary.c` now supplies the length-aware room boundary. It follows the
`bhSetRoom` / `bhSetRoomMtn` discovery order for the root MDL and room MTN bank,
bounds each package at neighboring offsets or the section end, and preserves
motion slot indices. Native work is published only after the entire load
succeeds. The caller retains the expanded room while any work references
its streams. The Quest check loads all ten RM_0000 room motion slots and
rejects cross-section offsets and late bank failures without losing the
previous room work. The same decoder now walks the enemy model lists, the
prop and item MDL packages, and each enemy motion bank. It retains bounded
SKIN and MASK payloads. The reserved first four prop slots and the sparse
motion slots keep their original indices.

`room_header.c` supplies the replacement data path for the original header
copy in `bhSetRoom`. It reads ROM scalar fields, checks the 16 room table
spans, takes the event-script location from RDT.hed03 rather than ROM's
stale authoring pointer, and clears ground slot 31 as the original does.
Camera cuts and their area tables get independent, mutable native storage;
changes to those records leave the source RDT untouched. The embedded
32-bit model-work record is not copied into runtime state.

The Quest compared every decoded scalar field and all table offsets/lengths
against the ISO-derived references for RM_0000 and RM_0140. This covers three
and six camera cuts respectively, including RM_0140's multi-area cuts and
31-by-31 route links. Malformed cut-area and cross-section table references
are rejected, and a failure late in a room load preserves the previous work.

The header loader also owns mutable native arrays for room lights, event lights
and event cameras. It preserves the original numeric fields, including light
link indices and camera visibility masks. The light runtime pointers and the
unused camera `recp` field start null; serialized PS2 addresses are never
converted to host pointers. Camera key counts and one-based continuation
indices are checked before publishing a new room. Other raw table views still
use disk strides.

On Quest, these records matched ISO-derived references for RM_0000, RM_0130
and RM_0140. RM_0000 supplied 12 room lights, 26 event lights and 39 event
cameras, including 33 nonzero camera pointer slots. RM_0130 supplied 12
nonzero light pointer slots. Tests also mutated native work without changing
disc bytes and verified that invalid camera records leave active work intact.

## Engine boundaries

- EE addresses and DMA tag fields stay 32-bit. Checked translation maps them to
  native host pointers, with bounds for the RAM and scratchpad ranges. The
  existing desktop build retains its original address mapping.
- The DMA check drives the real chain walker with recording test sinks. It
  exercises inline and referenced transfers, return from scratchpad to RAM,
  VIF tag payloads, register kicks and rejection of out-of-range transfers.
  It does not yet connect the walker to the live renderer or game.
- The original VU core already has a scalar backend. The ARM64 target exercises
  it with the upstream reference suite and disables floating-point contraction.
- The compile-time Ninja layout probe uses the original SDK declarations.
  Even with 32-bit scalar typedefs, native ARM64 pointers expand a model record
  from 24 to 32 bytes and an object from 52 to 72 bytes. The original records
  therefore cannot be cast to native structs or relocated in place with native
  pointers.

`room_engine.c` builds the original engine's `ROM_WORK`, `ML_WORK` and
`MN_WORK` from the bounded decoder. The room owns its mutable working copy
of the RDT and the native arrays. An unsuccessful load preserves the active
room. The Quest check verifies the RM_0000 model and motion bank through the
original types and calls the original `bhCheckCutArea` from `cut.c` to select
the three opening camera areas.

`room_scripts.c` bounds the original 32-bit scenario offsets against the
script section. The ARM64 branches of `bhInitEvent` and `bhControlEvent`
resolve room-entry and per-frame script pointers through this view, and the
original `Event_init` resolves numbered events before updating a task.
The Quest check compares three rooms against disc references, including all
39 numbered RM_0000 events. It exercises task dispatch, sparse and invalid
IDs, malformed-table rollback, and the original branch address arithmetic
above 4 GiB. A metadata scan separately checked the table format in all 205
rooms on the supported disc.

The Quest harness compared native enemy, prop and item work against RM_0000,
RM_0130 and RM_0140. It passes the decoded enemy motion banks to the original
`bhGetObjMotion` and checks the sampled keys. Malformed actor blocks and
oversized prop skin records leave the active room intact.

`room_skin.c` ports the bind-pose conversion in `njplus.c`. It checks the
joint records, weight table and vertex chunk before updating the model flags
and mutable coordinates. Quest matched the reference for 8,640 vertices
across RM_0000, RM_0130 and RM_0140. Invalid group, weight and vertex bounds
leave the active room intact.

The native branch of `bhSetRoom` stages the room from `sys->rdtp` and its
expanded byte count. It uses the original enemy, object and item constructors,
validates the texture directory and packet ranges, and queues model textures
in disc order with the original sharing rules. The native branch of
`bhFinishRoom` binds the owning or shared enemy motion bank, room motion and
facial data. `bhInitRoom` retires those views while preserving the four
reserved prop slots.

The Quest harness executes these handoff functions from `room.c` against
RM_0000, RM_0130 and RM_0140. It verifies native actor creation, texture order
and model-version sharing, motion reuse, arena alignment above 4 GiB and
teardown. A malformed texture directory, packet or TIM2 payload leaves the
staged room intact.

Loaded `NJS_TEXNAME` references now hold native pointers. The texture heap
stores its ring links as 32-bit offsets, keeping the extended TIM2 header
at its original 208-byte layout. On Quest, the original `bhSetRDT` and PVP
loader registered 89 model and effect texture references from RM_0000.
The test runs the original alpha and indexed-pixel conversion on the raw
TIM2 assets, and verifies release, compaction and save/restore above 4 GiB.
The opening-room effect registration still uses a test palette buffer.

The original TIM2 upload routine now exposes the image and CLUT transfers
to the native host. The offscreen Quest check routes them through the
existing GS memory code for all 32 indexed textures of the RM_0000 root
model. It compares decoded RGB values against the disc and checks exact
RGBA readback from GLES on Adreno 740, covering both the direct indexed
and converted 32-bit transfer paths.

The ARM64 `Ps2TexLoad` sends the original texture register writes to the
host without using the PS2's fixed DMA work address. On Quest, the
original texture selectors and the existing GS primitive assembler feed
an offscreen GLES sprite draw for each of the 32 textures. Rebinding the
same texture reuses the transfer, and an invalid selection leaves the
active binding intact. The original render-to-texture allocator captures
a 512Ã—512 draw, binds its generated TIM2 texture, and redraws the same
RGBA pixels. The test uses a dedicated sprite backend. The full room
renderer, Android presentation and the rest of `bhFinishRoom` remain
unverified.

The original Ninja matrix stack uses native pointers on ARM64 for matrix
copies, transforms, and vector operations. Rotation uses the original Ninja
angle table and order. The Quest check runs the original `bhCalcTree` against
the expanded opening-room model and compares all 67 world matrices with a
disc-derived reference. It also checks the authored eye position, direction
and object visibility for all three fixed cuts through the original
`bhSetRenderCut`, `bhGetCameraPosition` and `bhMakeCameraVector`. These
checks cover the transforms before room polygon submission and camera
projection.

The ARM64 Ninja draw adapter walks the original object hierarchy and submits
position, normal and UV data from the opening room's bounded vertex and
polygon chunks. On Quest, an offscreen 320Ã—240 draw of the third authored
camera cut submitted 1,274 strips with 8,006 vertices and bound all 32 root
textures through the original TIM2 path.

The room draw now uses the original Ninja diffuse/ambient decoder and GS
blend lookup. The offscreen Quest check uses the authored ambient values
and color calculation 4. It verifies the
59 strips that use the darker material and submits the 12 translucent
strips after the opaque pass. A separate pixel readback checks GS
modulation, the opening room's source-alpha blend, and rejection of
zero-alpha texels.

The room draw defers translucent strips using the original GS depth
coefficients and ordering-table rules. The Quest check matches the bucket
and texture order for all 12 strips, including the two entries that share
a bucket. A second GLES readback checks two overlapping strips submitted
near first and composited far first.

The Quest check runs the original room light controller and VU0
`CALCPOINT` routine. For the third opening-room cut it selects one
directional and two point lights. The VU implementation matches the
microcode interpreter, and the authored point-light pulse changes
39,447 pixels across two offscreen frames.

Native room rendering now shares the original Ninja bounding-sphere test
and rejects strip packets whose transformed vertices all lie outside the
same clip plane. It uses the same camera parameters for culling and GLES
projection. At the third opening-room cut this reduces 1,274 candidate
strips to 81 submissions while preserving the measured pixel coverage
and lighting response. Quest tests cover rejection on all six planes,
child traversal after parent culling, equal-depth draw order and the
visible portion of a strip that crosses the near plane.

Crossings on all six planes now use the original `_Check_ScissorPlane`
path and VU0 node routines. The native renderer emits the resulting
view-space fans and suppresses the strip triangles they replace. The
third opening-room cut submits 169 replacement fans. Its measured
coverage differs by one pixel from the earlier near-only scissor path.
Quest tests verify GPU readback and interpolation of position, UV and
lighting on each plane, and verify a vertex formed by clipping two
successive planes. Translucent depth ordering also passes.

The original camera fog setup sends FOGCOL through the native GS register
path. The ARM64 room adapter computes fog from the original depth table
and carries it through VU scissor interpolation. On Quest, the fog settings
from the first opening-room cut changed 76,292 pixels when applied to the
fixed test view with geometry and lighting held constant. An independent
pixel check verifies the GS fog equation, alpha preservation, and the RGB
byte order. Fog interpolation checks cover all six planes and a corner
formed by two planes. Remaining special effects, lighting modes, and
live Android presentation still need integration.

## Original facial-control state (v0.45)

Original `face.c` initialization, expression/lip selection and per-frame control
bodies now build for ARM64. Bounded typed bindings replace the packed exp0
pointer fields; those four scalar words stay zero. Each actor owns its controller
header and lip state, leaving disc data immutable. Room teardown clears the
registered exp0/exp1/exp2 references before freeing storage.

The dedicated check invokes the original controller on a staged 31-node facial
hierarchy, temporarily scoping the actor model reference and restoring it after
every tick. It covers original pause/repeat/one-shot/reset/freeze gates, lip
selection during playback, all original parameter groups, actual room MaskSet
bytes, malformed/foreign pointer rejection and rollback after deformation failure.
These ticks do not replace the live actor model or establish per-frame gameplay
publication. The six native checks and player/actor/equipment GPU regressions also
pass on Quest.

Build the dedicated check after the app objects, then run it with the ISO folder:

```powershell
& ./arm64/Build-FaceStateCheck.ps1
# Push arm64/build/check-face-state-arm64 to /data/local/tmp and chmod 700.
# Run it with /sdcard/Android/obb/org.recvx.arm64 as its argument.
```

## Original event-camera selection (v0.46)

The native dispatcher now calls original `bhCamSet` for unlocked event cameras.
Bounds checks cover mode/stage, camera and key counts, object/light hide-list
capacity and finite camera parameters before any scenario execution. Unsupported
lock/return/custom-volume modes reject explicitly. Original `bhSetEventCamera`,
visibility, fog, background lookup, near/far lookup and default clip-volume setup
run unchanged. The native room adapter already owns expanded camera/key records.

The original VU screen-projection and near/far setters now build for ARM64.
Clip-matrix VU loads, display-bound stores and GS clear-register writes use native
addresses instead of 32-bit address casts. The host initializes the original 4:3
screen and passes original near/far state and background registers to GLES.

The dedicated Quest check covers all 62 opening camera keys in eight variants:
496 complete camera-state comparisons, visibility masks, fog transition timing,
background enable/disable, native matrix/VU transfer and independent projection
values. It also executes actual RM_0000 CamSet bytes, original authored clip and
background overrides, malformed/truncated commands, unsupported modes and invalid
indices. Rejected requests preserve camera/system state; a rejected camera prevents
even a preceding flag command from executing. GPU readback verifies visible room
geometry and exact background color after native clipping removes that geometry.
The full six-check native suite and actor/player/equipment/facial regressions pass.

```powershell
& ./arm64/Build-CameraCheck.ps1
# Push arm64/build/check-camera-arm64 to /data/local/tmp and chmod 700.
# Run it with /sdcard/Android/obb/org.recvx.arm64 as its argument.
```

This ports camera selection, not the remaining automatic event-camera motion or
campaign handoff. The complete opening remains gated at BgmOnEx, byte 1794.

## Original ADX music and BgmOnEx (v0.47)

The original `bhBgmOnEx` calls original `PlayBgmEx`/`PlayBgmEx2`: stop a playing
stream, request its initial volume/fade, start the selected member, and update
original BGM metadata. Native validation checks the owned slot, mounted BGM1
archive, attenuation-table range and encoded member before dispatch. The original
sound-disabled flag retains its complete no-op behavior. Full script preflight
still runs before any scenario command; an invalid BGM request prevents even a
preceding flag command from executing.

The native adapter calls original CRI `ADX_GetCoefficient`, `ADX_DecodeMonoFloat`
and `ADX_DecodeSteFloat` C bodies. It validates type-3/version-3 headers, channels,
rates, sample extents, copyright marker and authored loop ranges before preparing
immutable stereo PCM. Other header versions remain gated. Prepared PCM is capped
at 128 MiB, with 32 MiB encoded-member bounds. This is a bounded native transport,
not the original middleware's asynchronous I/O scheduler; cache replacement and
broader streaming behavior remain pending.

Two owned native ADXT handles replace packed middleware assumptions. Callback
mixing reads prepared PCM without file I/O or allocation and reports completion
from samples consumed, with loop playback, mono conversion and rate adaptation.
Original `ExecAdxFadeManager` runs twice per 30 Hz game step, retaining its 60 Hz
sound-server timing and original attenuation table. Android/OpenXR focus loss
suspends output and both sample/fade clocks; this host lifecycle control is
separate from the original retail ADXT_Pause no-op.

Quest checks pass all 32,768 fade-byte/valid-volume request combinations, the
three actual opening BgmOnEx instructions, 25 complete fade timelines, loop PCM
repeat, one-shot end/stop, a mono 11,025 Hz disc sentinel, malformed/truncated and
foreign/wrong-slot inputs, sound-disabled no-ops, pause/resume and actual AAudio
source consumption. Each opening track's first two seconds matches an independent
float reference exactly: 576,000 signed PCM samples total, zero mismatches. The
six native executables and camera/player/actor/equipment/facial checks also pass.
FFmpeg decoding was inspected as an additional comparison; its integer predictor
is not the original CRI float decoder and is not the parity reference.

```powershell
& ./arm64/Build-BgmCheck.ps1
# Push arm64/build/check-bgm-arm64 to /data/local/tmp and chmod 700.
# Run with the ISO OBB directory; an optional second argument writes PCM captures.
python arm64/Check-BgmPcm.py arm64/build/bgm-reference
```

The Python check expects user-disc members `45.adx`, `83.adx`, `114.adx` and matching
`45.quest.pcm`, `83.quest.pcm`, `114.quest.pcm` captures in that directory. No game
content is embedded in the check. Deferred room music/ambience consumers, the
complete sound server, voice behavior and campaign/movie audio transitions still
need porting. BgmOnEx passing preflight does not start music in the gated campaign.

## Original frame waits and camera pause (v0.48)

The dispatcher admits original `bhFrameCheck` with a six-byte instruction. Its
selector guard accepts the player (ignoring the record byte as the original does),
remapped RDT enemies in the 128-entry native pool, and the 32 original object work
slots. Unsupported selectors and missing/out-of-range work reject before any
scenario execution. The handler itself is unchanged: compare the signed 16.16
current frame against the encoded whole-frame value, adding half a frame for any
nonzero half-frame byte. It returns true while below the boundary. It does not
advance animation or implement a new wait scheduler.

Original `bhCamPauseSet` also runs unchanged: selector zero sets camera mode 3,
selector one sets mode 1, and all other byte values retain the original no-op.
This ports the state-setting handler; live automatic event-camera interpolation
remains pending.

Quest checks cover every 16-bit threshold with/without a half frame, exact equality
and adjacent signed values, signed wrap, all 128 remapped enemies and 32 object
slots, ignored player selectors and every half-frame byte. Rejected/malformed
requests prevent preceding flag commands from executing. Player/enemy/object
canaries and full camera-state preservation pass. Total predicate/state counts
are 393,791 and 1,024 respectively.

The dedicated ISO check executes the actual opening FrameCheck in the original
While/yield/continuation machinery, verifies native continuation pointers above
4 GiB, and releases the wait at exact equality. Original native player motion
supplies walk, pause, fractional and reverse-wrap frame states for further wait
checks. The original scheduler yield is retained. Diagnostic state is restored
before teardown; no campaign progression flags are forced. The six native tests
and camera/player/actor/equipment/facial/BGM integration checks pass.

```powershell
& ./arm64/Build-FrameCheck.ps1
# Push arm64/build/check-frame-arm64 to /data/local/tmp and chmod 700.
# Run it with the ISO OBB directory as its argument.
```

Full opening preflight now stops at BgmOff, byte 2152. These read/state commands
do not establish the remaining actor motion or camera playback integration.

## Original BgmOff stop and fade (v0.49)

The dispatcher calls original `bhBgmOff` and `StopBgm` unchanged. Zero stops the
native ADX source immediately; a nonzero operand passes its value times ten to
original `RequestAdxFadeFunction`. Original BGM number/current/next volumes reset
as authored. This command does not suppress requests when the sound-disabled
flag is set: it still stops or schedules the fade, while the original sound-server
policy holds fade advancement until sound processing resumes.

Original details are preserved: an immediate stop does not clear an existing
fade controller, a fading stop leaves `AdxPlayFlag` unchanged even at completion,
and this direct command does not rewrite the deferred request word used by
BgmOff2. Native readiness checks require the owned active stream configuration;
fading also validates the current volume before the original attenuation-table
path can run. A stop does not require a mounted archive or decoded track.

The dedicated Quest check runs every fade byte across playing/idle streams,
four starting levels and sound-disabled/enabled states: 4,096 complete timelines.
It checks whole stream records, original metadata/markers, pending immediate-stop
fades, final silence and actual room instruction bytes. An intermediate fade
preserves source position and produces PCM matching original table attenuation
within one signed sample unit. Foreign/wrong-slot/sleeping/uninitialized contexts,
invalid fade volumes and malformed commands reject without state changes; a
rejected stop prevents a preceding flag command from executing. Immediate stops
retain the original behavior of leaving an unused invalid volume untouched.
Lifecycle suspension, restarting during a fade, bank-free idle stopping and live
AAudio source-stop timing also pass. The six native tests and all existing
player/actor/equipment/camera/facial/frame/BGM integration checks pass.

```powershell
& ./arm64/Build-BgmStopCheck.ps1
# Push arm64/build/check-bgm-stop-arm64 to /data/local/tmp and chmod 700.
# Run it with the ISO OBB directory as its argument.
```

Complete opening preflight now reaches PlCtr, byte 2182. The full scenario stays
gated, and the deferred room-audio consumers remain pending.

## Original PlCtr position and task controls (v0.50)

The dispatcher now uses a bounded variable-length PlCtr table. Only original
subcommands 0 (no-op), 7 (player position), 12 (position signs) and 13 (rotation
signs) are admitted. Original `bhPlCtr` and these `Player_controll` C cases are
retained, including the outer task-mode change and ignored padding. Other cases
remain excluded from the native core and reject during full scenario preflight;
the non-native switch remains unchanged.

A borrowed, registered `EXP_WORK` binding verifies the published player and its
exact 124-byte scalar context without truncating pointers. Layout assertions
retain original backup offsets 72/80. Positioning also checks finite floor-table
entries and verifies the task still targets the player at dispatch. Teardown
clears the binding before the owned player/work expires. Task sign/no-op commands
need no actor dereference; an unsupported or malformed subcommand never executes.

The original position case retains unsigned hundredth-unit coordinates, task
signs, position backups, scalar X/Z backups and original floor selection. Quest
checks cover every 16-bit coordinate value across eight sign combinations:
524,288 cases with whole player/scalar/allocation canaries. All 256 task-sign
bytes, ignored padding, no-op behavior, unsupported/truncated commands, occupied,
foreign and retired scalar references and nonfinite floor tables also pass.

The ISO integration check executes all three original position instructions from
RM_0000. After initializing effect draw state, scripted-player rendering is byte
identical to independently assigned native world position; hiding the player
changes 3,168 pixels. A task reassigned to a foreign target rejects before the
position handler writes player/scalar state. Binding teardown and complete-room
progression preservation pass, as do the six native executables and existing
player/actor/equipment/camera/facial/frame/BGM checks.

```powershell
& ./arm64/Build-PlayerScriptCheck.ps1
# Push arm64/build/check-player-script-arm64 to /data/local/tmp and chmod 700.
# Run it with the ISO OBB directory as its argument.
```

Complete opening preflight now stops at CommonCtr.25, byte 2202. PlCtr standing
motion, control handoff and other scripted action modes remain pending. These
position/task commands do not establish the full opening-to-gameplay transition.

## Original common player motion and model setup (v0.51)

The original `Common_controll` cases 25 and 47 now compile in the native event
core. Case 25 retains the original speed table, mode/counter writes, motion
reset and eight motion/model/link callback combinations. The native subset
accepts player bank mode 0 with interpolation mode 6. Other bank/actor modes
and interpolation remain gated. Requested control mode and the motion slot
actually evaluated remain distinct, as in the original handler.

The adapter publishes the owned native motion bank through `sys->plmthp`,
checks decoded track provenance and model/matrix/stack capacity, and clears
its references before player teardown. Equipment replacement updates the
registered hand owner. Original linked-model traversal compares full-width
native parent pointers; supported children are the owned hand pair and original
shadow effect. Other player-linked families reject before source execution.
Desktop builds retain their original EE address comparison.

`Build-CommonMotionCheck.ps1` builds the isolated Quest check. All 2,048
speed/update combinations pass against independent double-precision hierarchy
math: 458,752 matrix channels, maximum error 0.000003026. Original room
instructions at bytes 2202 and 2210 pass; model-only recalculation leaves
motion time and linked work unchanged. Foreign banks/tracks/matrices, exhausted
stack space, invalid pool bounds, unsupported actor/interpolation modes and
same-low-32-bit foreign parent pointers are checked. Whole-room preflight still
runs before any scenario instructions and stops at PlCtr.52, byte 2216.

All six native suites and the player, actor, equipment, facial, camera, frame
and music integration checks pass on Quest. One AAudio check missed its fixed
160 ms startup deadline; the unchanged rerun passed with 7,680 consumed samples.
The new source callbacks evaluate motion/model/link state in isolation. Their
world matrices are not yet connected to continuous native skin publication in
the original frame order. This milestone does not establish campaign readiness.

## Original standing reset and control handoff (v0.52)

`Player_controll` now retains original cases 52 and 128 in the native core.
Case 52 calls the unchanged `bhStandPlayerMotion`: health/injury state selects
the original idle table entry, modes/flags and scalar X/Z backups reset, then
the original motion evaluator and model calculator produce the frame-zero pose.
Case 128 restores normal motion speed and the registered player bank, updates
original control flags/modes, and falls through to the original task-release
body. Direct subcommand 139 remains gated. No replacement handoff is injected.

The native adapter validates scalar and motion-bank identity. Standing checks
bound the weapon group before indexing the original table and validate the
selected clip/model/matrix stack before source execution. Dispatch rechecks
the task target for both commands. Complete room preflight continues to reject
unsupported commands before running any scenario instructions.

The common-motion device check now also verifies 352 combinations of both weapon
groups, health threshold neighbors and integer extremes, all injury-bit states
and arbitrary old damage levels. Frame-zero unsigned angle keys, root translation,
129,536 world matrix channels and complete player/scalar state match independent
references; linked hands/shadow remain untouched by standing reset. Another
4,356 flag bit/complement combinations verify control handoff and source task
release after original LoadWork. Actual RM_0000 bytes at 2216/2218, invalid
groups/scalar/track/bank rejection and reassigned delayed-task rejection pass.

The next complete opening dependency is original `bhInitCamSet` at byte 2238.
Continuous scripted motion/skin updates and original frame ordering remain
pending; isolated source handoff checks do not validate a playable opening.

All six native suites and player/actor/equipment/facial/camera/frame/music
integration checks pass for v0.52. The music check now waits at most two seconds
for asynchronous AAudio source consumption, retaining the original sample-count
and playing-state assertions; its prior fixed 160 ms deadline was intermittently
missed after GPU stress. No production audio timing changed.

## Original room-camera initialization and return (v0.53)

The unchanged `bhInitCamSet` now runs its original player position snapshot,
`bhCheckCut(1)`, view and light updates. `bhCamSet` selector 1 restores the
original room camera and clip range; its two trailing bytes remain ignored and
it leaves view/light publication to the subsequent original update. The earlier
native `bhSetCut` exclusion of room clip/background reset is removed. Default
culling volume and authored near/far/background consumers run as in the source.

`bhCheckNativeCameraInit` borrows the existing native room owner and retires
with it. The guard checks cut/area counts and pointers, model/light identities,
registered player scalar/model state, native view basis/matrices, floors, finite
records, active-camera extents and light link joints before original callbacks.
Native cut-area allocations retain their immutable capacities separately.
Player-eye initialization, paused cut tasks, unvalidated linked/animated lights,
and rooms with authored enlarged culling volumes reject before scenario work.
The custom-volume room check follows the original RM table's rcase-independent
matching. Other CamSet selectors and event-camera target locks remain gated.

`Build-CameraInitCheck.ps1` exercises the actual opening instruction. Quest
passes all 256 InitCamSet padding values, 54 positions/versions against independent
cut geometry and visibility checks, and 512 return-camera operand/clip states.
View matrices, camera world positions and direction vectors match independent
math (maximum matrix error 0.000004936 in the padding pass). The resulting shot
renders 296,152 nonblack GLES pixels. Foreign banks, area capacity, view/scalar
pointers, floor/cut indices, nonfinite/degenerate records and unsupported modes
reject atomically before a preceding flag command. Truncated initialization is
also rejected. Native plane-point copies retain full-width addresses; the
isolated event target uses the fixed 4:3 cone rather than a desktop window query.

Complete opening preflight now stops at CommonCtr.24, byte 2300. These isolated
camera commands do not establish automatic per-frame scenario publication or a
playable campaign opening.

All six native suites and camera/player/actor/equipment/facial/frame/music
integration checks pass for v0.53, including 128 independent view-plane normals
and full-width source-point copies.

## Original room-motion setup for opening actors (v0.54)

The original `Common_controll` case 24 is retained in the native event core.
Its supported subset uses actor mode 1 and interpolation operand 6. Original
speed/default behavior, low-byte motion assignment, control fields and eight
motion/model/link callback combinations are unchanged. Update selector values
8–255 retain their original no-callback behavior. The two real opening actors
use 27-node models; the command may animate a bounded prefix of those joints.
The original slow, normal and fast evaluators select the frame-zero pose from
all ten authored room clips, preserving trailing joints and null-track behavior.

The room owner now retains decoded root-motion metadata until destruction.
Checks match native headers, attribute and track pointers to those owned records.
After the original arena model-work allocator runs, actor joint arrays are
registered with the native room owner; command dispatch verifies their exact
identity alongside actor remapping, geometry, matrices and stack capacity.
Unsupported modes/interpolation, companion actors, existing pose blending,
node-remap skips and linked-model families reject before source callbacks.
Whole-room validation still occurs before scenario execution.

`Build-RoomMotionCheck.ps1` builds the dedicated device check. Quest passes
4,752 commands covering all speed bytes/eight update branches, all ten clips,
both actors and the ignored callback selectors. Full actor state and independently
derived frame-zero keys match; 953,344 world matrix channels have maximum error
0.000013184. Actual opening bytes at 2300, malformed/foreign clip-bank-track-model
references, unsupported blend/link modes, truncated commands, reassigned tasks
and retired-room rejection pass. These checks validate command setup and immediate
pose/model evaluation; continuous actor animation/skin/render publication remains
pending. Complete opening preflight advances to CommonCtr.34, byte 2318.

All six native suites and room-motion/camera/player/actor/equipment/facial/frame/
music integration checks pass for v0.54.

## Original explicit room-animation frame selection (v0.55)

The original `Common_controll` case 34 now compiles in the native event core.
Its supported mode-1/operand-6 subset retains the original speed table, control
fields, low-byte clip assignment, frame load and motion/model/link callbacks.
The full ten-byte command is bounded before operand access. The room owner
validates the selected clip frame plus the evaluator's wrapped next frame;
frame count limits prevent signed 16.16 overflow. CommonCtr.24 continues to use
frame zero through the same owned bank/model checks. Unsupported actor modes,
existing pose blending, companions and linked-model families remain gated.

`Build-RoomFrameCheck.ps1` exercises every authored frame of all ten opening
clips on both characters, all speed bytes and callback branches, ignored
callback selectors and final-frame-to-first-frame key reads. Quest passes
16,166 source command executions. Whole actor state, attributes and keys match
independent references; 953,344 independent matrix channels have maximum error
0.000014514. All 65,536 encoded frames are checked against the clip bound;
per-clip limits, all truncation lengths, foreign banks, unsupported modes/blends,
nonfinite wrapped neighbor keys and reassigned tasks reject before handler writes.
The actual opening instruction selects frame 77 correctly. Retired-room checks
also reject the borrowed context.

Full opening preflight advances to original SubCtr (0x67), byte 2482. These are
isolated frame-selection/evaluation checks; continuous actor animation, skinning,
facial updates and rendering still need their original per-frame ordering.

All six native suites and explicit-frame/room-motion/camera/player/actor/
equipment/facial/frame/music integration checks pass for v0.55.

Installed v0.55 also reaches a focused OpenXR session on Quest, receives live
Touch/player locomotion input, and logs the expected opcode-0x67/byte-2482
whole-scenario gate. This confirms startup/input, not the unfinished opening.

## Original actor-control handoff and task release (v0.56)

The unchanged `bhSubCtr`/`Sub_controll` handlers now retain native cases 0, 128,
139, 144 and 149. Dispatch bounds their complete two-/four-byte encodings.
Case 128 restores `sys->emtp[id]`, updates the original modes/control flags,
clears only the low byte of `ct2`, and falls through to the original task-release
case 139. Case 144 preserves the motion-bank reference and selects mode3=3.
Case 149 retains the original zero/nonzero control-flag operand and ignored
padding. Task-only release/no-op never access the task's actor pointer.
Other SubCtr cases remain gated, including the legacy companion pointer path.

The existing native room owner validates actor mapping and target identity.
Before bank-restoring handoff, the referenced bank must be an owned enemy package
or the room's existing owned empty fallback for that enemy ID. Null targets,
player work, foreign pointers/banks and retired contexts reject before source
writes. This does not complete missing enemy-bank assets or automatic enemy AI;
actors without a separate native enemy package still use their existing empty
fallback. The original source state handoff and task release are verified in
isolation while the full scenario remains gated.

`Build-ActorControlCheck.ps1` runs 152,596 original-handler cases against
independent whole-actor and whole-task references. Checks cover both opening
actors, all flag bits/complements, low-byte clearing with signed/high-byte
counter canaries, preserved modes/frames, all 256 flag operands and padding
values, full-width bank identity and exact task release. Scheduler integration
executes the actual room bytes at 2482; unsupported commands/truncation/foreign
mapping/banks/null/reassigned targets reject, and task-only operations safely
ignore foreign work. Complete opening preflight now advances to original
`bhEasySESet` (0x86), byte 2578.

All six native suites and actor-control/explicit-frame/room-motion/camera/player/
actor/equipment/facial/frame/music integration checks pass for v0.56.

Installed v0.56 reaches a focused OpenXR session and logs the expected live
EasySESet gate at opcode 0x86/byte 2578. Full opening execution remains disabled.


## v0.57 — original event MIDI requests

The original EasySESet handler now supports its type-7 event sound path with
owned player/enemy/prop/item joint checks. Original PlayGameSe4Event,
CallNativeEventSe, MIDI request/server and fade/pan bodies supply the behavior.
The native transport carries start, parameter and stop requests into the shared
SQ/SPU2 software synthesizer. Only the owned room MIDI bank 2 is mounted; shot
banks, other sound families, SDK timers, sound environments/reverb and changing
room sound packs are not completed by this milestone.

The SDK server runs before fade/pan managers, matching ExecSoundSynchProgram:
a new fade value reaches the mixer on the following sound-server phase. The
original native-event call also resets current pan after calculating pan speed;
that behavior is retained. Trajectories that could exceed signed-byte conversion
bounds are rejected during full preflight, rather than approximated.

Build the dedicated check with `./arm64/Build-EasySeCheck.ps1 -SkipBuild` after
building the app objects. On Quest, the real opening request matches 96,000
sample values from independent driver-command playback using the same original
assets. This checks the native software transport, not PS2 hardware bit accuracy.
All six native suites and the actor/player/camera/equipment/facial/frame/music
integration regressions pass. The dedicated check also verifies original asset hashes, 65,537 volume/pan states across
five slots, 16,383 atomic pan-range rejections, all 256 fade durations (33,152
server phases), pause/exact PCM resume, original stop, bank retirement/remount,
invalid-family/slot/volume/joint/foreign-bank/truncated-command rejection and the
real AAudio callback. Full opening preflight advances to VoiceOn at byte 3112;
unsupported code still prevents any scenario prefix from executing.

Installed v0.57 startup was verified on Quest: the OpenXR theater reached focused
session state 5, and New Game loaded the room with the expected VoiceOn preflight
gate at byte 3112. The session subsequently lost focus and returned to idle;
the app process remained alive. See `build/v57-live-startup.log`.


## v0.58 — original scripted dialogue

Opcode 0x19 is VoiceOn, not SeOn (0x17). The v0.57 blocker label was incorrect;
its byte offset and refusal to execute the incomplete opening were correct.

The original eight-byte VoiceOn handler, PlayVoice/PlayVoiceEx2, VoiceOff,
StopVoice, ADX fade manager and stream-completion block now run with owned native
handles. The host mounts VOICE1/VOICE2 according to the original disc flag. It
prepares bounded immutable mono PCM before a handler can change game state.
The opening's nonspatial mode 1 is supported. Positional modes remain gated;
room sound-bank transitions, other sound families and complete voice/movie/event
coordination still need work. Status/time query commands remain unported.

The original monitor's ADX loop is shared with the host without invoking the
unported enemy/object sound managers. Native playback updates run the completion
monitor before scenario work, while sound fades continue through the existing
60 Hz sound phase. Stream flags are cleared by the original monitor, not by the
audio callback. Original stop and fade flag semantics are retained.

`./arm64/Build-VoiceCheck.ps1 -SkipBuild` links the dedicated device check after
building the app objects. The check covers 8,192 complete request states (four
owned source selections, active/idle, four levels, all fade bytes and padding),
all 256 task ownership markers, both actual opening dialogue instructions,
all 256 stop fade bytes at four levels (203,700 original fade phases), one-shot
completion, exact music/voice summation and independent stopping, paused clocks
and exact PCM resume, mono channel capacity, malformed/unsupported/foreign
references and real AAudio playback/stop. All six native suites and sixteen
integration checks pass.

To compare captured PCM independently, pass an output directory as the device
check's second argument, pull its `voice-16.pcm` and `voice-17.pcm`, then run:

```powershell
python arm64/voice_reference.py "Resident Evil - Code - Veronica X (USA).iso" PCM_DIRECTORY
```

Both one-second captures match the independent original-float ADX calculation:
192,000 sample values, zero differences. This validates native decoding and
transport; it is not a claim of PS2 hardware bit accuracy. Full opening preflight
advances to original MessageSet (0x1f), byte 3130. Campaign execution stays gated.

Installed v0.58 startup was verified on Quest: OpenXR reached focused session
state 5, New Game loaded the original starting room with the expected MessageSet
preflight gate at byte 3130, and Touch input drove the original player locomotion
states. No crash appeared in the process log (`build/v58-live-startup.log`).


## v0.59 - original room messages and font rendering

The original MessageSet (0x1f), bhSetMessage, bhControlMessage, bhDispFont and
MesDispEndSet (0x5c) bodies supply message setup, counters, timing, page handling,
font positions, advances, colors and textured geometry. The original message source
only needed missing function/variable declarations. A native room wrapper exposes
the owned message span. The adapter bounds
room text to its owned table and adjoining room sections. It validates indices,
relative offsets, word alignment, terminators, supported controls, line width,
line count and glyph capacity before permitting original handlers to run.
Item insertion, choices and unsupported glyphs/controls remain gated.

The controller runs before scenario work in the native game update, matching the
original message/event ordering. Its generated glyph packets are captured once
per update and presented separately, so stereo/repeated rendering cannot advance
text twice. The renderer uses the original font atlas, packed GS coordinates,
UVs, colors and point sampling on the 640x480 game picture. Native page cursors
must identify the initial text or an authored page boundary; foreign, unaligned
and cross-message cursors are rejected. Failed controller collection restores
message flags/counters/cursor, leaving the previously captured frame intact.

Build `./arm64/Build-MessageCheck.ps1 -SkipBuild` after building app objects. Its
Quest checks verify all 50 original room messages, 143,872 setup states, all
trailing operand bytes, control-only representatives, every end-display padding
byte, independent glyph positions/advances/UVs/colors in all three authored
positions, timed cue retirement, manual page confirmation, text reveal speed,
malformed and foreign references, retired context, and GLES readback. The final
font capture contains 5,064 visible pixels in the authored HUD band; repeated
presentation is identical and leaves original message state unchanged.

All six native suites and seventeen integration checks pass, including the
existing player, actor, camera, equipment, effect, voice and MIDI/ADX paths.
The full opening now preflights through the message commands and reaches
FadeSet (0x36), byte 5248. No part of the incomplete campaign script executes.
The playable scene is unchanged; the message GPU check is isolated from campaign
progression. Its optional second argument writes a 640x480 RGBA capture for QA.

Installed v0.59 was launched on Quest and reached focused OpenXR session state 5.
No crash appeared in the process log (`build/v59-live-startup.log`). The complete
opening gate and message GPU path were verified by the separate device checks.


## v0.60 - original screen fades

The original FadeSet (0x36), bhSetScreenFade, bhControlScreenFade and
bhDrawScreenFade bodies supply packed ARGB setup, previous-target color, float
origins, counters, interpolation, endpoint clamping and the original fullscreen
quad. The native source adaptation adds a missing declaration header and excludes
the PC-only overlay wrapper around that quad. Original fade math is unchanged.

The host advances fade state once per game update, before message/event work,
and captures the original polygon through the bounded 2D transport. GLES draws
its original colors and rounded GS alpha over the game picture. Repeated/stereo
presentation consumes the saved polygon without advancing state. Transparent
endpoints omit the overlay; opaque endpoints remain visible after the completion
flag clears. The virtual room itself is outside this game-image draw path.

The adapter validates channels before source float-to-integer packing, bounds
active byte-duration countdowns and checks prospective channel steps. Malformed
commands fail full script preflight before a preceding instruction can execute.
Invalid controller state leaves source state and the captured frame intact.

Build `./arm64/Build-FadeCheck.ps1 -SkipBuild` after building app objects. On Quest,
65,536 requests cover every alpha/duration byte, RGB permutations, fractional
origins, previous-target packing, replacement of obsolete state and six-byte
consumption. Eight rising/falling/fractional trajectories at all 256 durations
match 261,128 original controller phases, including delayed first updates and
endpoint snap. Interrupted fades and the opening's actual transparent-black
instruction also pass.

GLES checks all 256 alpha values against independent original packed GS blend
math (at most one output-byte rounding difference), exact full black across all
307,200 game pixels, and identical repeated rendering with unchanged source state.
The real host room/message path has 296,152 visible pixels with a transparent
fade and becomes entirely black at opacity. Truncated/nonfinite/out-of-range
commands and invalid countdown/origin states are rejected atomically.

All six native suites and eighteen integration checks pass. Full opening preflight
now reaches CommonCtr subcommand 34 with motion mode 2 at byte 5506
(`69 22 00 02 03 00 00 06 0a 00`). The existing selector-1 motion path is still
validated; selector 2 remains gated. The complete opening script is not executed.

Installed v0.60 was launched on Quest and reached focused OpenXR session state 5.
No crash appeared in the process log (`build/v60-live-startup.log`). The complete
opening gate and fade integration were verified by the separate device checks.


## v0.61 - original explicit-frame actor motion modes 2 and 3

CommonCtr subcommand 34 now accepts original modes 2/3 with interpolation operand
6 for owned non-companion room actors. The original handler, motion evaluator,
model calculation and link-update dispatch are unchanged. The native guard now
recognizes that these branches overwrite the full motion number and clear the
actor's pending blend before evaluating a pose. Mode 1 retains its original
high-byte clip selection and inherited-blend guard. CommonCtr subcommand 24's
additional modes, interpolation and companion actors remain gated.

`./arm64/Build-RoomFrameCheck.ps1 -SkipBuild` checks all three explicit-frame
modes across both actors, all frames of all ten room clips, every speed byte,
update branches and final-frame neighbor wrap. The 48,498 commands compare
complete actor state, node metadata and poses against original key data;
2,860,032 world/link matrix channels match independent double-precision hierarchy
calculations (maximum error 0.0000145139159). Modes 2/3 are exercised with stale
high clip bits, nonzero prior blending and distinctive counters/control bytes,
verifying full clip replacement, blend clearing and preservation of unrelated
control state. All 65,536 encoded frame values are checked in each mode.

The actual opening instruction at byte 5506 selects clip 3/frame 10 and preserves
the original main control/counter state. Unsupported modes/interpolation,
foreign banks, malformed/truncated commands, invalid frame/track data, reassigned
task targets and retired room context remain guarded. All six native suites and
eighteen integration checks pass, including the existing GPU, player, camera,
message, effect and audio paths.

Full opening preflight now reaches EffModeSet (0x91), byte 5690. This milestone
verifies isolated original motion commands; it does not enable the full campaign
or complete per-frame scripted skin/render publication.

Installed v0.61 startup was verified on Quest: OpenXR reached focused session
state 5 and Touch input drove original player locomotion states. The process
remained alive through focus loss and resumption. No crash appeared in the
process log (`build/v61-live-startup.log`). The dedicated native suites and
integration checks completed on the device before installation.


## v0.62 - original effect mode commands

The original four-byte EffModeSet (0x91) handler writes only mode1 through the
original room-record-to-pool mapping. Its body is unchanged. Native effect-table
publication now registers the owning room, source table, exact slot mapping and
record/work identities. Retirement of that owner disables subsequent commands.
A second publication cannot replace the active context or partially change the
pool; discarding a staged owner leaves the active mapping intact.

Preflight checks the registered mapping, live work identity and self pointers,
then invokes the native controller's mode validator. Supported persistent room
controllers retain their original mode behavior. Emitter 125 remains limited to
its seven supported child variants (plus the zero/no-cue value), overlay modes
require valid reciprocal depth, and expiring particle/shadow targets remain
gated. Original effect update and draw routines consume the mode normally.
No substitute smoke/particle logic is added.

`./arm64/Build-EffectModeCheck.ps1 -SkipBuild` verifies 5,936 whole-work states
across all 14 original room records, mode bytes and padding, plus 1,488 atomic
unsupported-emitter rejections. The actual opening instruction triggers its
original type-2 smoke manager and one-shot particle. Separate lifetime checks
verify paused/loading cues, original fall/expiry, initialized UV/overlay owners
and 87 visible Quest GPU pixels with identical repeated rendering and unchanged
state. Publication overlap, invalid indices, truncated commands, foreign tables,
same-family remapping, inactive/mismatched work, self pointers, invalid prospective
modes and owner teardown are checked before original writes.

All six native suites and nineteen integration checks pass. Full opening
preflight now reaches EasySESet (0x86), sound family 1, at byte 5820:
`86 01 01 40 40 80 80 00 00 01 01 1a 00 00 2c 01`.
This sound path still needs its original playback/bank integration. The full
campaign script remains disabled; isolated effect tests do not advance campaign
progression.

Installed v0.62 was launched on Quest and reached focused OpenXR session state 5.
No crash appeared in the process log (`build/v62-live-startup.log`). The complete
opening gate and effect-command integration were verified by separate device checks.


## v0.63 - original immediate walk/run sound requests

The existing owned room SPQ loader now publishes its original bank-2 shot header
and body alongside MIDI assets. Shot bank 2 uses body port 5, matching the
original SE_BANK mapping. Headers/body extents and program tables are bounded;
occupied or foreign banks are rejected. Bank retirement stops its shot slots
and voices before freeing the shared pack.

Original shot registration allocates twenty owned handles after the eight MIDI
work records. The original shot SDK server loop, request allocator, fades/pan
managers and player footstep function supply behavior. Native adaptation avoids
negative shifts for idle shot channels, excludes PC extra-bank bits, preserves
request-mask clearing, and carries original request/change/cancel packets to the
shared shot/ADPCM mixer. The original idle-handle cancel targets an absent driver
port and is consumed without playback, as in the driver.

EasySESet family 1 currently supports immediate, explicit volume/pan walk/run
requests for three original IDs and five floor programs. Nonzero fade durations,
volume/pan sentinels, other step/action modes and unsupported sound environments
remain gated. Original slot alternation, random pitch, paired volume/pan state,
SDK parameter conversion and server-before-fade ordering are retained. The
original camera/player sound-position block is shared with the native host.
Spatial input is bounded before original distance/pan table reads; the authored
pan table's unsupported angle tail and the distance-table boundary remain gated.

`./arm64/Build-FootstepCheck.ps1 -SkipBuild` checks original disc hashes, 7,680
walk/run requests (all three IDs, five floors, two types and 256 RNG seeds),
16,129 explicit volume pairs, 255 pan targets, complete paired controller and
SDK state, alternating/retriggered channels, pause/exact PCM resume, original stop,
active-bank retirement/remount, malformed/foreign references and the AAudio
callback. Ten original floor/walk/run cases match independent native driver
commands across 960,000 stereo sample values, including first-buffer versus
following-phase parameter timing. This proves native transport conformance with
the shared software backend, not PS2 hardware bit accuracy or audible in-game
movement integration.

All six native suites and twenty integration checks pass. The Windows ADB daemon
briefly disconnected during the integration batch; its connection was restored
and all interrupted/remaining checks completed successfully.

Full opening preflight advances from byte 5820 to byte 5846, where the next
family-1 instruction requests source joint 30. The current source actor's primary
native model exposes 27 joints. Additional model/joint binding work is required;
the guard is retained instead of substituting a joint or reading beyond owned
matrices. The complete campaign script remains disabled.

The v0.63 APK is installed on Quest 3 (versionCode 63 / versionName 0.63).
Live startup reached focused OpenXR session state 5; the startup log contains no
fatal exception or native crash. The playable scene remains unchanged.

## v0.64 - original expanded facial model binding

The original bhFinishRoom keeps actor work, then bhInitMask calls
fmCnkInitContext. That setup inserts tooth, tongue and eye nodes, increments
obj_num from 27 to 31, flat-packs the resulting hierarchy and replaces joint
work. The native facial owner already prepared this expansion, but the room
script actors still held the 27-node source descriptor. At the corresponding
post-allocation stage, the room now publishes the owned facial model descriptor
and matrices to mdl[0], retaining the other actor fields and original mbp cache.

Sound-source ownership uses the expanded model's exact object/work identity and
count. The original bhEasySESet therefore reads its actual requested joint 30.
Room-motion ownership uses the same model; the guard admits the nine original
protected nodes (6 through 14), bounds consumed tracks to the remaining 22
entries, and rejects altered protection flags anywhere in the 31 entries.
The original motion and model/link handlers are unchanged. Flat entries retain
original source-tree links; those links serve as traversal markers in bhCalcTree.

The room-motion and explicit-frame device references traverse the private
hierarchy for parent relationships and independently evaluate the animated flat
entries. They check 4,752 motion commands and 48,498 explicit-frame commands on
both actors/all ten room clips, including the short three-track clips. Across
4,358,144 independent world/link matrix channels, maximum error is below
0.000012563. The tests reject all 31 altered protection bits. Footstep checks
verify both model bindings, all 62 owned actor/joint sources with admitted
listener directions, actual opening alternating-footstep commands, out-of-range
joint 31, a truncated 27-node descriptor, foreign work and nonfinite joint 30.
Original spatial-table bounds remain enforced. Existing 7,680 request states,
16,129 volume pairs, 255 pan targets and 960,000 PCM values still pass.

Full preflight advances from byte 5846 to byte 6540: PlCtr.131, first operands
01 00. That player-control family is not enabled. This binding fixes script
model ownership and isolated motion/sound execution; it does not enable the
complete opening or continuous scripted skin/render/facial publication.

All six native suites and twenty integration checks pass with the final build.
The Windows ADB daemon briefly timed out during cleanup, recovered, and all
remaining device checks completed successfully.

The v0.64 APK is installed on Quest 3 (versionCode 64 / versionName 0.64).
The initial launch was intercepted by the Quest controller-required dialog.
After the user opened the app, live logs verified focused OpenXR session state 5
and New Game room preflight at opcode 0x64, byte 6540 (PlCtr.131). No fatal
exception or native crash appeared in the captured app log. The session later
lost focus and returned to idle; the app process remained alive. The playable
scene is unchanged.

## v0.65 - original scripted player action selection

Original Player_controll case 131 is compiled for native events without changing
its body. The four-byte PlCtr instruction sets mode0=7, copies action/variant
bytes into mode2/mode1, and derives mode3 from whether the selected action was
already active. Action 1 always clears mode3. The event length table admits the
original instruction; preflight requires the owned native scalar player context
and runtime dispatch requires the exact published player as the task target.
Other unported player cases remain excluded from native compilation.

Build-PlayerModeCheck.ps1 builds the new check-player-mode-arm64 device helper.
It checks 131,072 whole-player records: all 256-by-256 incoming/selected action
pairs and all selected-action/variant pairs. It verifies forced action-1 restart,
repeat-action continuation, unrelated scalar work, the four-byte instruction
length/continuation marker, and both actual opening requests at 6540 and 6558.
The fixture resumes an original event task with its published player target,
avoiding the independent LoadWork player-mode reset obscuring the incoming mode.
Truncated commands and foreign scalar work reject before a preceding flag
instruction can run. Null, foreign and enemy task targets reject before touching
player state; teardown retires the scalar context.

Full room preflight now stops at byte 6694, LoadWork selector 4, effect record 9.
Its following CommonCtr commands position and translate the effect. The existing
native effect registry owns that work, but work selection and shared common
transform dispatch do not yet accept it. The guard is retained. The player's
selected scripted actions also require later live runtime integration; this
state-selection change does not enable the complete opening or campaign.

All six native suites and twenty-one integration checks pass. The original
scalar player suite's unsupported-case list was updated for newly supported
case 131; truncated and retired-context rejection checks were added. The
corrected event suite and remaining effects suite then passed. Final event
results are in v65-events-suite-device.log; v65-suite-device.log retains the
earlier stale-test failure for diagnosis. The game runtime is unchanged by
that test-only correction.

The v0.65 APK is installed on Quest 3 (versionCode 65 / versionName 0.65).
The initial launch was intercepted by the Quest controller-required dialog.
After the user opened the app, live logs verified focused OpenXR session state 5
and starting-room preflight at opcode 0x65, byte 6694 (effect work selection).
No fatal exception or native crash appeared in the captured app log. The session
later lost focus and returned to idle; the app process remained alive. The
playable scene is unchanged.

## v0.66 - original room effect work and scripted transforms

The original bhLoadWork selector 4 resolves the room record through sys->efid
and retains model_cno=0, including the source handler's ignored padding byte.
Native preflight resolves through the active room effect registry first. The
shared registry checks the room/source/count, exact record/slot/work identities,
active allocation, self pointers and supported persistent controller state.
Outputs stay untouched on rejection. Runtime effect task targets must resolve
back to a published room record; arbitrary active pool slots are insufficient.

Original Common_controll bodies are unchanged. Effects admit only scalar
subcommands 0/2/3/5/6/7/11/12/13/23/41: no-op, relative translation/rotation,
step/sign setup, absolute position/rotation, and existing flag masks. Shared
prefix offsets are asserted against native O_WRK. Original unsigned operands,
signed signs, backups, floor selection and wrapped 32-bit rotation are retained.
The native guard checks finite floor data and translation inputs/results.
Model/root/motion operations stay gated. Linear entry preflight remembers an
effect selection and rejects unsupported common operations before any prefix;
runtime repeats the restriction for reassigned work or modified instructions.

Build-EffectWorkCheck.ps1 builds check-effect-work-arm64. Quest checks all 14
room records/all 256 LoadWork padding bytes without changing the pool or mapping,
and 28,672 whole-effect transform sequences across every record, eight signs and
all byte values. It executes the actual opening position/step bytes and the
suspended 250-iteration For/Next trajectory. On GLES, an intermediate moved frame
matches independently positioned native effect work exactly; 11,711 pixels
change and 9,978 are visible. Repeated presentation leaves work and pixels
unchanged. Malformed/out-of-range/remapped/retired/foreign selection, missing
allocation, nonfinite/overflow translation, invalid floor state and unsupported
effect model operations reject. Teardown retires registry references.

Full opening preflight advances to MovieStart (0x5e), byte 6916, followed by the
original movie-status predicate. Startup movie playback already works; event
movie coordination is still unported. This change does not enable partial
campaign scripts or establish complete opening/campaign compatibility.

Final v0.66 validation passes all six native suites and twenty-two integration
helpers. The new effect-work helper also checks 79,872 whole-effect flag cases:
all records/display selectors/padding and all 65,536 word-controlled operands.
Final logs are v66-suite-device.log and v66-<helper>-device.log.

The v0.66 APK is installed on Quest 3 (versionCode 66 / versionName 0.66).
Live startup reached focused OpenXR session state 5; no fatal exception or
native crash appeared in the captured startup log. The playable scene is
unchanged; complete opening execution remains gated at MovieStart.

## v0.67 - original event movie coordination

Original bhMovieStart (0x5e, four bytes), bhMovieStop (0x5f, two bytes) and
bhMovieCk (0x9b, six bytes) are admitted for the native movie target. Start
preserves the original request-only behavior: it writes mvi_no and raises
cb_flg bit 0x4000000. Stop clears that pending request bit; it does not invent a
new active-decoder stop operation. MovieCk retains the original predicate:
CheckPlayEndMovie returns nonzero while playback is active. Ignored operands,
instruction lengths and conditional/suspended-task behavior are preserved.

The movie request handoff is extracted unchanged from bhSysCallEvent and called
by both the original system task and the native coordinator. The original
bhSysCallMovie cases 0 through 6 drive preparation, start/buffer/play, cleanup and
exact task/screen restoration. Native adaptation uses uintptr_t for 64-byte arena
alignment. The decoder owns separate CPU/GPU storage, so its no-eviction path
sets mvi_flg=0 and excludes PS2 GS texture backup/reload. Original StopBgm and
StopVoice calls stop the native ADX streams before playback; native retained
stream handles do not need the PS2 decoder's Sleep/Wakeup hardware eviction.
Decoder failure returns a checked host error and teardown restores saved tasks;
the original broad return-to-title error path is not enabled by this adaptation.

The room owns the event movie context. Preflight checks original table bounds,
the available disc member and supported movie mode before request execution.
Runtime rejects foreign decoders, overlapping requests, invalid phases and a
replaced arena mark. Disc startup/teardown binds/retires the native video source.
The host keeps event movies on the game's 30 Hz update, services the original
controller once per tick, renders captured messages over the video, and releases
movie graphics/held input on return. Startup video retains its existing path.
Task dispatch is suspended/restored by the original sp_flg bits during movie
preparation and playback. Full campaign scripts remain blocked by preflight.

Build-MovieEventCheck.ps1 builds check-movie-event-arm64. Quest checks 65,536
request padding pairs, all 256 movie selectors (18 disc members available),
original flag/length/state preservation and atomic malformed/missing-file
rejection. Actual opening bytes start movie 0, reach the first second on the soundtrack clock,
produce a nonempty GLES image, and freeze both frame/time and pixels during
pause. Original While/MovieCk/EvtNext/Ewhile2 waits remain suspended while active
and resume after stop. Cancellation restores the original phases, full-width
arena mark and saved task/screen bits. MV_004 completes naturally through frame
444 with exact task restoration. The native controller is tested separately
from the still-gated complete opening.

Full opening preflight advances to EffClearEvt (0xca), byte 7282. General event
effect retirement, live scripted skin/actor/player/facial publication and the
remaining campaign runtime still need work; this is not complete campaign
compatibility or the original opening running end to end.

Final v0.67 validation passes all six native suites and twenty-three integration
helpers. Movie checks additionally cover all pending-stop padding bytes,
truncated request/status/stop atomic preflight, invalid movie mode, cancellation
before the first displayed frame, foreign decoder/arena/phase, overlapping
requests, pending controller retirement and rebind. Final logs are
v67-suite-device.log and v67-<helper>-device.log.

The final movie helper also composites an original room description over the
video (5,064 glyph pixels), then verifies an exact clean video frame after the
font renderer. The opening's message 49 is a control-only cue, so the visible
composition check uses original room message 0.

The v0.67 APK is installed on Quest 3 (versionCode 67 / versionName 0.67).
The initial launch was intercepted by the Quest controller-required dialog.
After the user opened the app, live logs verified focused OpenXR session state 5
and starting-room preflight at opcode 0xca, byte 7282 (EffClearEvt). No fatal
exception or native crash appeared in the captured app log. The session later
lost focus and returned to idle; the app process remained alive. The playable
scene is unchanged.

## v0.68 - original selective event effect cleanup

Original bhEffClearEvt (0xca, two bytes with ignored padding) calls original
bhClearEventEffect, which invokes bhClrEff_YT, bhClrEff_RY and bhClrEff_YG.
The handler and all four cleanup bodies remain unchanged. Native compilation retains the two
additional original clear passes from effsub2.c/effsub3.c while excluding their
unported effect update/draw families. It does not replace selective cleanup
with a pool reset, remove room records/mappings or clear render queues/caches.

The original passes target 24 effect IDs and touch only active entries. Most
only clear flg; IDs 22/24/25/27 additionally clear a nonnull exp0 word, IDs
254/260 always write exp0, and ID 302 writes exp1 and exp0. Native preflight and
runtime scan the fixed pool before calling any pass. The nullable cases are
admitted only with exp0=null; the mandatory expansion-write families remain
gated until native constructors provide owned payloads. Foreign, aliased and
null mandatory payloads therefore cannot cause partial cleanup or an unsafe
write. Untargeted entries and inactive/reserved work are preserved exactly.

Build-EffectClearCheck.ps1 builds check-effect-clear-arm64. Quest checks 198,144
whole-work cases: every 16-bit ID with inactive/full/high-bit flags and every
flag bit/inverse for the 24 target families. It exercises all 512 slots,
low/high/aliased expansion references, missing mandatory payloads and an invalid
payload introduced after preflight. Rejection leaves a preceding script flag
and the entire pool untouched; truncated commands also reject atomically.
All 256 padding bytes and both actual opening cleanup sites preserve the 14
published room effects and mappings. Render queues and initialized work remain
unchanged, and the GPU frame is byte-identical across 306,239 visible pixels.
The comparison snapshots state after the renderer's initial reciprocal-depth
clamp, preserving that original draw behavior.

Full opening preflight advances to ItmAspdSet (0x42), byte 7670. Unowned legacy
expansion payloads, remaining command/runtime modes and continuous scripted
actor/player/facial publication remain pending. No partial campaign execution
is enabled by this milestone.

Final v0.68 validation passes all six native suites and twenty-four integration
helpers, including effect cleanup, movie playback and prior render/audio/player
checks. Logs are v68-suite-device.log and v68-<helper>-device.log.

The v0.68 APK is installed on Quest 3 (versionCode 68 / versionName 0.68).
Live startup reached focused OpenXR session state 5; no fatal exception or native
crash appeared in the captured startup log. The playable scene is unchanged,
and full opening execution remains gated at ItmAspdSet.

## v0.69 - original item angular-speed command and explicit runtime readiness

Original bhItmAspdSet (0x42, four bytes) is dispatched without changing its body.
It reads an unsigned item slot and unsigned speed byte, skips the final padding
byte, and replaces the item's int aspd field. The operand is a byte, not a
16-bit speed. The original bhFirstGameStart allocates 32 native O_WRK entries
for both object and item pools. Retention now verifies their full aligned
extent lies inside the owned game arena. Item-speed dispatch requires the exact
retained item base, native room record table/count and slot below 32, including
unused original slots. Reassigned, shifted or retired pools reject before writes.

Build-ItemSpeedCheck.ps1 builds check-item-speed-arm64. Quest verifies 2,097,152
whole-item updates over all 32 slots, every unsigned speed byte and all 256
ignored padding values. Each case starts with a high-bit int sentinel to prove
full field replacement; neighboring records and all other item fields remain
unchanged. Four authored commands at 7670/7674/7798/7802 set slots 0 and 4 to
100 or zero. Invalid slots, truncated commands, null/foreign/shifted pools,
foreign room metadata, invalid counts and a pool reassigned after preflight
reject atomically. Room teardown retires the item-speed context.

With this instruction admitted, all 41 entries / 8,248 bytes of the owned opening
bank pass rx_events_validate_room in the initialized native context. This checks
instruction/operand coverage, not continuous execution or every future state.
Previously an unsupported opcode implicitly prevented opening execution. Now
rx_opening_check_events separates command validation from live runtime readiness.
It returns RX_OPENING_LIVE_RUNTIME (a non-byte readiness marker, not an opcode
from the disc) after command preflight succeeds. The opening reports
commands_ready=true and events_ready=false, and logs the missing continuous
actor/player motion, skin and controller publication rather than inventing a
new unsupported instruction. The readiness check does not execute a prefix or
change progression/player state. Isolated supported script fixtures retain the
original dispatcher for device testing. The playable scene is unchanged.

The next major work is original continuous scripted actor/player update and
native skin/render publication, followed by the remaining camera/facial/AI and
campaign systems. Completing a room's opcode preflight does not make the full
original opening playable.

Final v0.69 validation passes all six native suites and twenty-five integration
helpers. Existing device fixtures now check the named live-runtime readiness
marker after successful opening command preflight. Logs are
v69-suite-device.log and v69-<helper>-device.log.

The v0.69 APK is installed on Quest 3 (versionCode 69 / versionName 0.69).
Live startup reached focused OpenXR session state 5 without a fatal exception
or native crash in the captured log. Complete opening command coverage was
verified by the device helper; live scripted opening execution remains disabled.
The playable scene is unchanged.

## v0.70 - original continuous scripted actor motion/model phase

The native target now links original subpl.c. A bounded actor phase invokes
original bhBackupEnemyTransform and bhSubpl, including callback 6, bhSetMotion
and bhCalcModel. bhControlEnemy shares the extracted unchanged six backup
assignments. Supported states are mode 1 with motion callback 6 and frozen modes
5/6 with callback 6, for owned room motion banks and native model/work arrays.
Reset and continuing branches retain original clip/frame, attribute and blend
reset semantics. Fractional interpolation, signed steps, single wrapping,
nine protected facial joints and world-joint matrices use the original engine.

Every selected actor is checked before the first callback. Foreign banks,
clips, models, callbacks, blend/link modes, hierarchy markers, matrix overrides,
nonfinite/extreme coordinates and out-of-range clocks reject without partial
batch updates. Inactive and original stopped states skip; sp_flg bit 2 controls
the actor phase. The host reserves this phase before the event scheduler, as
in original enemy-before-event task order. It remains behind live readiness.

Build-ActorTickCheck.ps1 builds check-actor-tick-arm64. Quest verifies 13,174
cases across both opening actors, all ten room clips, advancing/frozen modes,
reset/continuing callbacks, every integer frame plus fractional/endpoints,
forward/reverse/zero steps, protected joints and 6,745,088 independent matrix
channels (maximum error 0.000017829). Eighteen malformed second-actor batches
reject before any actor or pose writes. Original suspended FrameCheck/While/
EvtNext fixtures release from the actor clock at the exact half-frame threshold,
remain suspended while paused and preserve reverse-wrap predicate semantics.

This ports the continuous motion/model portion, not the complete bhControlEnemy
task: facial control, damage/AI dispatch and native skin/draw publication remain
pending, alongside player script motion and remaining controller modes. Complete
opening command preflight still passes, but events_ready remains false. No
campaign prefix is enabled and the visible playable scene is unchanged.

All six native suites and twenty-six integration helpers pass on Quest 3.
Logs: v70-suite-device.log and v70-<helper>-device.log. v0.70 is installed
(versionCode 70); live startup reaches focused OpenXR session state 5 with no
fatal exception or native crash in v70-live-startup.log.

## v0.71 - original actor skin publication and draw-stage integration

Native facial pose output can now consume already-evaluated world matrices.
The shared native translation of npInitCalcSkinFM / npCalcSkinFM subtracts actor
position from joint translations, cancels local joint rotation for the partial
blend, maps joints above 10 past the four inserted facial nodes and blends
positions/normals in original VU operation order. It preserves packet metadata,
facial source vertices, actor clocks and published world-joint matrices.
Repeated publication starts from the facial source and cannot accumulate skin
transformations. The earlier detached sampler uses the same skin kernel with
zero actor translation; its existing reference suites still pass.

Original EasyMultiDrawTreeCnk semantics are retained: the skinned owner uses
view plus actor translation, and rigid packets use view times world joints.
All selected visible actor skins are checked before any output is written.
The bounded adapter currently admits original EasyMulti facial actors with
the original model-culling bypass bit. Alternate draw types, late model recalculation, linked roots, morphing,
custom culling, mirror and special lighting modes remain explicitly gated.
The host draw pass publishes/draws room actors behind opening readiness, using
a separate GLES texture cache that is released on graphics teardown or equipment
changes. Existing room, player and weapon cache indices remain stable.

Build-ActorSkinCheck.ps1 builds check-actor-skin-arm64. Quest verifies 480 world
poses across both actors/ten room clips, fractional and endpoint frames,
translated/rotated actors and all ten skin weights. Its independent reference
checks 4,402,080 position/normal channels (maximum error 0.000003797), plus
2,880 draw parts / 46,080 matrix channels (maximum error 0.000017029).
Actor state, world joints and facial source bytes remain unchanged; repeat
publication is exact. Nine malformed second-actor batches reject before either
skin buffer changes. The fixed-camera/fixed-lighting GLES fixture renders the
two textured original actors (28,623 changed pixels), produces identical repeat
frames and releases its separate actor cache. This fixture alone removes the
large authored root translation to put the models next to Claire for visibility;
production retains the original authored stage-space root offset.

Original continuous facial updates, player script motion and remaining actor
controllers are still pending. The complete opening bank continues to pass
command preflight; the live opening remains disabled and the normal playable
scene is unchanged. No campaign prefix is enabled by this milestone.

Final v0.71 validation passes all six native suites and twenty-seven integration
helpers on Quest 3. Logs: v71-suite-device.log and v71-<helper>-device.log.
The APK is installed (versionCode 71 / versionName 0.71); live startup reaches
focused OpenXR session state 5 without a fatal exception or native crash in the
captured v71-live-startup.log. Full opening remains gated.

## v0.72 - live original facial updates in the actor task

The bounded room actor phase now follows original bhControlEnemy ordering:
transform backups, bhControlMask, then bhSubpl/bhSetMotion/bhCalcModel. The face
controller copies original jaw/tongue/eye angles into the protected flat joints
before body motion. The existing native skin/draw path consumes the resulting
world pose and current facial source. One original simulation sub-loop is set
by the host for each 30 Hz actor/event tick; frame rate is unchanged.

Whole-batch validation now includes facial state and its derived deformation
parameters. A preview invokes the original scalar controller with geometry
checks replacing deformation, then restores the control words, native MASK/head/
lip work, playing stream, lip base, actor flags, flat angles and VU state.
Validation therefore cannot advance the real expression or lip cursor, warm a
face cache, deform a source or advance an earlier actor. Muscle, jaw, tongue
and eye validation is separated from writes and shares the same checks with
actual deformation. Invalid initialization/freeze counters reject before the
original signed increment. Real face controller bodies remain unchanged.

Build-LiveFaceCheck.ps1 builds check-live-face-arm64. Quest verifies 3,822
combined expression/lip/body/world/skin samples, including 3,662 lip frames
across all 20 clips and 19 complete streams for both actors. Independent samples
check original facial parameters and cursor timing, and 1,895,712 matrix channels
check the protected-joint world hierarchy (maximum error 0.000010854). Preview
leaves actor/controller/lip/geometry/skin/joint/VU snapshots unchanged. Pause,
last-sub-loop geometry, unchanged-face freeze and forced updates retain original
semantics. Six invalid second-actor states, including a nonfinite parameter
computed from an owned base key, reject before either actor changes. Isolated
motion and skin fixtures disable their optional mask controller to retain
focused coverage; the new fixture covers the combined path.

Complete opening command preflight remains supported, but live opening execution
still awaits original player script motion and remaining continuous controllers.
The normal playable scene is unchanged and no campaign prefix is enabled.

Final v0.72 validation passes all six native suites and twenty-eight integration
helpers on Quest 3, including prior actor motion, original facial state and
stable GLES skin rendering. Logs: v72-suite-device.log and v72-<helper>-device.log.
The APK is installed (versionCode 72 / versionName 0.72); live startup reaches
focused OpenXR session state 5 without a fatal exception or native crash in the
captured v72-live-startup.log. Full opening remains gated.

## v0.73 - original per-frame scripted player movement

The native script-player path invokes original pl_evt.c controllers and shared
phases extracted unchanged from bhControlPlayer. The desktop driver also calls
those phases: transform/body-history backups, original bhSetMotion and end flag,
foot-lock correction, smoothed body offset, wall collision, safe-position/floor
bookkeeping, bhCalcModel and ground/attack-bound publication. Controller entries
0/1/2/5/8 and mode-8 hold are admitted only with supported state and owned motion
assets. Current scope is living unarmed Claire. Running/target-following and
other omitted callbacks, water/stairs, linked roots, knockback, player head/
weapon-patch/contact behavior and the normal-control handoff remain pending.
Native compilation excludes water effect dependencies only in the walking
routine; validation rejects those states rather than silently skipping effects.

A controller preview computes prospective state using the original routine,
checks the selected motion/frame/blend and restores player/scalar/flag-word/VU
state. Footsteps validate original owned floor/audio state during preview and
submit the original CallPlayerFootStepSe request only in the real update.
The host schedules scripted player work after enemy work and before events,
and prevents its existing locomotion preview from overwriting scripted motion
or running a second player update after an event changes the player state.
Full opening readiness remains disabled.

Player skin now shares one kernel for local pose sampling and scripted world
publication. Script publication consumes already-evaluated world joints without
advancing motion again. As in original npInitCalcSkin, it subtracts actor
translation and retains world rotation. The skinned body draws with view plus
actor translation; equipped hands use their world socket matrices without a
second player transform. Skin matrix scratch storage is owned per model node
count and released with the player. The existing local preview path is retained.

Build-ScriptPlayerTickCheck.ps1 builds check-script-player-tick-arm64. Quest
executes original RM_0000 event-26 position/rotation/controller bytes for 25
standing updates and 45 walking updates, checks blend completion and the
walk-to-stand transition. This is an isolated script fragment, without preceding
opening events; its collision outcome does not establish complete cutscene
placement. An analytic oracle checks 800 walking updates over eight headings
with collision disabled only for that fixture. Independent hierarchy checks
cover 320,160 matrix channels (maximum error 0.000018205). Original suspended
FrameCheck/While/EvtNext releases from the real player clock at the exact frame
boundary. Ten malformed contexts and the original pause gate preserve state.

World skin matches 4,428 independently transformed position/normal channels
(maximum error 0.000004370). Comparing old local and original world-relative
GLES paths produces 5,174 differing color channels with maximum delta 11/255;
the numerical geometry reference passes, and that GPU comparison uses an
explicit tolerance. Repeated world frames are identical. Hiding Claire changes
10,078 pixels, establishing visible player geometry in the test frame.

This milestone connects selected original scripted movement and positioning,
not the complete bhControlPlayer task or complete opening. Remaining actor/
player modes, full initialization/state tracing and the normal-control handoff
must be verified before enabling the opening or claiming initial gameplay.

Final v0.73 validation passes all six native suites and twenty-nine integration
helpers on Quest 3. The common-motion regression checks include all 352 standing
resets and 4,356 handoff-command cases. Logs: v73-suite-device.log and
v73-<helper>-device.log. The installed APK reports versionCode 73 / versionName
0.73. After the user woke the headset/controllers, a fresh startup reached
focused OpenXR session state 5 with no fatal exception or native crash in the
captured v73-live-startup.log. The earlier controller-required launch dialog is
recorded in v73-launch-system.log. Full opening remains gated and the normal
playable scene is unchanged.

## v0.74 - original normal entry and complete room placement

The native controller now dispatches original bhCPM0_action, bhControlPlayerPad,
bhCPM1_act_bas, bhCPM2_act_std and bhCPM2_act_srt for bounded normal idle/left/
right turn states. Original health-dependent turn rates, clip/blend changes,
idle clock and cache flags are preserved. Native compilation omits unsupported
attack/other action dependencies and water branches, with validation rejecting
those paths before the source runs. The 301st idle update would enter original
idle-gesture state 13; this continuation remains gated rather than being silently
clamped or skipped. Normal walking/combat/interactions are still pending in this
runtime; the existing default locomotion preview remains available separately.

Original normal-pad selection can modify system status flags, so controller
preview now restores those alongside player/scalar/flag/VU state. Runtime input
polling is separated from the preview's external pad selector: once the complete
runtime is enabled, original bhCPM0_action selects the pad state once. The normal
app path remains behind opening readiness.

The original player-placement block is extracted unchanged from bhFinishRoom and
shared by the host. It initializes authored or saved coordinates, safe positions,
body/collision history, floor selection, facing and smoothing state. The host
previously assigned position alone, leaving those scalar histories at zero.
Validation checks native spawn ownership, selector, floor values, character and
saved-placement bounds before calling the source block. Initial world joints
and skin are published after placement. This ports placement fields, not a
complete save/load or full original player initialization pipeline.

Build-PlayerIdleCheck.ps1 verifies both authored room spawns, the saved-position
branch and exact collision/safe-position history. Four malformed placement
contexts reject without changing player/scalar work. It checks 300 normal idle
updates and the explicitly gated gesture continuation, then 1,600 normal turns
over both directions/eight headings/five health boundaries. Independent hierarchy
checks cover 699,200 matrix channels (maximum error 0.000008591). Preview preserves
all snapshots, including system status and VU registers; normal walking is still
rejected and the original task pause is respected. The fixture services audio
once per update, matching the host instead of exhausting its sound queue.

Build-OpeningRuntimeCheck.ps1 executes the unmodified opening bank in an isolated
native process, temporarily enabling its runtime only inside the test. It sends
ordinary periodic confirm input and the original movie-skip button after playback
starts, without setting progress flags to bypass prerequisites. The trace passes
initial normal idle/movie setup and now stops at CommonCtr (0x69), byte 5294:
"Original room actor linked-model update is not ported yet". The helper asserts
this explicit boundary; it is not a successful-opening or playability assertion.
The installed app retains its live-runtime readiness gate.

Final v0.74 validation passes all six native suites and thirty-one integration
helpers on Quest 3. The equipment roundtrip fixture restores its initial world
pose space before its unchanged exact pixel comparison. Logs are
v74-suite-device.log and v74-<helper>-device.log. v0.74 is installed
(versionCode 74 / versionName 0.74); fresh startup reaches focused OpenXR session
state 5 with no fatal exception or native crash in v74-live-startup.log.
The full opening remains gated at the linked-model readiness dependency and the
normal playable scene remains available through the existing host path.


## v0.75 - original room actor linked models

Original bhCalcLinkModel now runs for owned room props/items attached to room
actors and bounded original shadow effects. Its unchanged original traversal
uses native full-width identities, then bhCalcModel/bhCalcTree for model targets
and joint-offset/rotation matrices for effects. Object rotation-reset behavior
remains distinct from the original effect branch. This extends the bounded
CommonCtr.24/34 runtime; linked characters/hands and other effect families remain
explicitly unsupported.

Retention captures each prop/item hierarchy alongside its arena-owned matrices.
Runtime validation requires the retained object/item pools, model and work
identity, valid parent-joint indices, finite transforms/keys/matrices, matching
balanced hierarchy and enough matrix stack. All selected children validate
before the original command can update its parent or an earlier child.

Quest verification passes 20,480 prop/item updates across 40 parent/model
combinations, both actors, all eight update selectors, 64 headings and both
object rotation-reset branches. An independent double-precision hierarchy
checks 499,712 matrix channels (maximum error 0.000005881), repeated link-only
updates are exact, and non-link selectors preserve child matrices. Another 512
shadow updates check the separate original effect semantics. Malformed joint,
transform, model/work/pool, hierarchy, matrix and stack cases reject before pose
writes, including a bad late effect following a valid prop.

The isolated real opening trace, with ordinary confirm/movie-skip input, now
passes CommonCtr.24 at byte 5294 and stops at CommonCtr.47, byte 5304:
"Original model recalculation has a foreign task target". This is the existing
player-only recalculation guard encountering an owned room actor; its extension
is the next dependency. The trace is not a successful-opening assertion.
The installed app retains its full-opening readiness gate.

All six native suites and thirty-two integration helpers pass on Quest 3.
Evidence: v75-suite-device.log and v75-<helper>-device.log in arm64/build.
v0.75 is installed (versionCode 75 / versionName 0.75). After the Quest
controller dialog cleared, startup reached focused OpenXR session state 5,
with no fatal exception or native crash in v75-live-startup.log.


## v0.76 - original actor model recalculation

CommonCtr.47 now admits owned non-companion room actors to its unchanged
original bhCalcModel/bhCalcTree body. The native guard resolves the selected
actor through the retained RDT mapping, then validates exact model/joint-work
ownership, hierarchy, finite positions/matrices and matrix-stack bounds. Linked
actor roots and companion models remain unsupported. Static preflight retains
the existing player-context check; dispatch validates the selected room actor
again after task selection or a frame yield.

The original nonlinked-root flag 0x1000 preserves the retained root matrix.
Original joint override flag 1 retains that joint matrix and uses it for its
children. Model-only recalculation does not evaluate motion, advance its clock,
change controller/scalar state, change model pose keys or update attached props.
Those original semantics are preserved rather than replacing recalculation with
a motion sample or linked-model traversal.

On Quest, 8,192 recalculations cover both actors, 64 headings, retained-root and
computed-root branches, every one of the 31 joint overrides and the no-override
case. A separate double-precision hierarchy checks 4,194,304 matrix channels
(maximum error 0.000002532); repeated publication is exact. Thirteen malformed
actor/model/root/joint/hierarchy/stack contexts reject before pose writes, as do
foreign room mappings, foreign selected work after a frame yield and retired
room ownership. Existing player model-only and linked-model checks still pass.

The isolated opening trace now passes byte 5304 and runs 3,000 simulated ticks
without an opcode fault. It remains at event-loop byte 5353, with message 5
complete/waiting and cb_flg bit 18 clear. Original game.c contains outer-game
message confirmation/completion and interaction transitions that the host has
not integrated yet. The trace uses original message-confirm mask 0x800 instead
of the earlier helper's 0x200; corrected input alone does not complete the wait.
The helper asserts this bounded waiting state, not full opening playability.
No progress flags are forced, and the installed full opening remains gated.

All six native suites and thirty-three integration helpers pass on Quest 3.
Evidence: v76-suite-device.log and v76-<helper>-device.log in arm64/build.
v0.76 is installed (versionCode 76 / versionName 0.76). Startup reaches focused
OpenXR session state 5 without a fatal exception or native crash in the captured
v76-live-startup.log. The visible scene and full-opening readiness gate remain
unchanged.


## v0.77 - original outer-game message/task transitions

Original bhMainSequence now shares its complete post-model message/task scalar
block through bhGameMessageTransitions. Its body matches the original source
byte-for-byte, and the original main loop calls it at the same location. Native
compilation isolates this shared block from the remaining unported main-loop
render/controller dependencies. The native host calls it after actor/player
updates and before message/event processing, only in the gated live runtime.

The original ordered transitions cover pending message completion/cancel, door
gating, result flags, paused task and pad restoration, view flags, interaction
entry/task masks and fresh or alternate end-page confirmation. Confirmation
queues completion; the following tick retires it. The original branch writing
room light 1 requires the retained native light bank before any scalar writes.
Other branches retain their pointer-free behavior without unnecessary light
requirements. This does not implement the complete original main sequence,
interaction controller, camera or actor AI.

Quest verification compares 8,388,608 combinations of relevant message, task,
door, result, input, view and interaction flags against an independent ordered
transition specification. Unrelated flag bits, pad backups and whole observed
scalar/light state are checked. Original message/font processing reaches the
end page, fresh/alternate confirmation queues completion and the next tick
retires it. Foreign/null/short light owners reject before scalar prefixes;
pointer-free branches and teardown ownership checks pass.

The isolated opening trace now retires message 5 and restores actor tasks.
It reaches the explicit continuous-actor mode/bank guard, with event cursor at
byte 5363: actor 1 has callback 0, mode3 4 and the original enemy motion bank.
That normal actor path is the next porting dependency. No progress flags are
forced, and this trace is not an assertion that the opening or campaign works.
The installed full opening remains behind its live-runtime readiness gate.

All six native suites and thirty-four integration helpers pass on Quest 3.
Evidence: v77-suite-device.log and v77-<helper>-device.log in arm64/build.
v0.77 is installed (versionCode 77 / versionName 0.77). Startup reaches focused
OpenXR session state 5 without a fatal exception or native crash in the captured
v77-live-startup.log. The visible scene and full-opening readiness gate remain
unchanged.


## v0.78 - original held actor handoff

Original MotionPauseSet/SubCtr.128 can restore an enemy motion bank while
leaving callback 0 in a held state. Original mv00_subpl0 deliberately has no
motion-sampling branch for modes 3/4. The native path now calls that unchanged
callback through bhSubpl: body keys and clock stay fixed, transform backups and
world matrices update, and original facial control and skin publication continue.
It does not substitute an idle clip or sample the newly restored enemy bank.

The whole actor batch validates callback identity, owned enemy-bank identity,
owned model/work/hierarchy, bounded finite transforms and original facial
context before any real callback. Linked roots, joint matrix overrides and
animated normal enemy-bank states remain explicitly gated. Unused motion
selectors are not dereferenced by the held source callback; switching back to
an animated state invokes the separate guarded path.

Quest checks cover 4,096 held updates across both actors/modes, 2,097,152
independent world matrix channels (maximum error 0.000011555), original
pause/handoff instruction setup, original transform backups and exact body
pose/clock preservation. Another 160 face/world/skin samples cover all 20
expression clips for both actors while body clocks remain fixed. Fifteen
malformed second-actor contexts, including foreign banks/callbacks/model work,
invalid hierarchy/joints and unbounded transforms, reject before any actor
prefix. Original task pause remains a no-op and animated bank states reject.

The isolated real opening trace now passes the held actor handoff and reaches
the next player continuation guard around simulated frame 1600. Original
standing logic attempts idle gesture action 13 after 300 idle updates; the
preview rejects that unported continuation and restores the original player
state. No progress flags are forced. The installed full opening remains gated,
and animated normal enemy-bank callbacks still need a separate port.

All six native suites and thirty-five integration helpers pass on Quest 3.
Evidence: v78-suite-device.log and v78-<helper>-device.log in arm64/build.
v0.78 is installed (versionCode 78 / versionName 0.78). Startup reaches focused
OpenXR session state 5 without a fatal exception or native crash in the captured
v78-live-startup.log. The visible scene and full-opening readiness gate remain
unchanged.


## v0.79 - original player idle gesture and ARM64 foot locking

The native original basic-action dispatcher now includes action 13 and calls
unchanged bhCPM2_act_sta. Original standing requests it on update 301. Entry
clip 9 (20 frames) blends into loop clip 10 (45 frames) at the authored final
entry frame. Original gesture flags, body locking, blending, health-driven
clip/frame mapping, directional cancellation and standing return are preserved.
All 15 directional masks may interrupt the locked gesture; the next ordinary
walking input still reaches the explicit unported-movement guard.

Validation checks both owned gesture clips before the controller can read their
headers. Player clips now also require exact native track-table identity before
any track dereference. The preview restores controller input in addition to
player/scalar/head flags, system status and VU state, including the directional
bits the original gesture clears during interruption. Counter validation admits
the original 301st-update gesture request while guarding signed overflow.

Executing original gesture foot locking exposed a native-address truncation in
njSubVector: its translated EE-register branch truncated high stack pointers.
The native scalar branch loads both input triples before storing and keeps the
original scalar clamp semantics without routing native pointers through 32-bit
EE words. It preserves same-pointer/partial-overlap behavior and VU state.
The original desktop/PS2 branches remain available under their existing build
conditions.

Quest checks pass 8,192 high-address vector cases, including aliases, partial
overlap, neighbor preservation, overflow and denormal clamps. Gesture checks
pass 3,304 idle/entry/loop/interruption samples over eight headings, 792 poses
compared with independent original disc keys and 1,215,872 independent world
matrix channels (maximum error 0.000008469). World-relative skin publication,
all three states, fractional/reverse clocks, all 15 directional cancellations,
five health boundaries and nine malformed contexts pass. The existing normal
entry fixture now asserts the original 301st-update gesture request instead of
expecting that continuation to fail.

The isolated original opening trace runs 3,000 simulated ticks without a runtime
fault, reaches the original idle loop and accepts forward input to cancel it.
The following held-forward update reaches the normal action/interaction guard:
original normal forward walking is the next dependency. This bounded trace
skips the movie through its original input path and does not establish complete
opening/campaign playability. No progress flags are forced. The installed full
opening remains behind its live-runtime readiness gate.

All six native suites and thirty-six integration helpers pass on Quest 3.
Evidence: v79-suite-device.log and v79-<helper>-device.log in arm64/build.
v0.79 is installed (versionCode 79 / versionName 0.79). After the Quest
controller dialog cleared, startup reached focused OpenXR session state 5
without a fatal exception or native crash in v79-live-startup.log.


## v0.80 - original forward walking, turns and full-angle footsteps

Original basic actions 3/4/5 now call bhCPM2_act_wlk for forward movement and
walking turns. The original controller keeps its motion-group/health clip
selection, frame-5 entry, blending, original per-frame translation speed,
turning rate, body smoothing, footsteps and shared static wall collision.
Running, pushing, reverse movement, water effects and other unsupported
controllers still reject before real player/controller changes.

The native player owns the original translation bank and retains descriptors
for its three forward-speed blocks. Binding publishes sys->plzmtp and teardown
clears that borrowed view. Before the original bhGetTransZ traverses size words,
validation checks every preceding retained header, the exact owned bank, source
selector, prospective clip/frame and selected speed. Both current and
health-selected pose clips validate before controller header reads. Original
water-effect calls remain in the desktop/PS2 path; native water guards reject
those states before the controller can reach the excluded effect dependencies.

Static wall preflight keeps owned room/floor views, bounded finite geometry,
collision bounds and supported wall families. Unported trap hit reactions are
explicitly gated. The original collision kernel handles the admitted geometry;
it is not replaced by the analytic test wall.

Quest checks pass 11,520 updates across two motion groups, three health levels,
eight headings and all three forward/turn input patterns. Independent decoding
of authored speed blocks checks speed and displacement. Another 10,368 settled
poses match disc motion keys; 672 footstep ticks match the original foot tables.
An independent hierarchy checks 4,239,360 world matrix channels (maximum error
0.000020123). A synthetic box inside the owned test room validates original
actor/body clearance and stable sustained wall contact. Fifteen malformed
bank/header/speed/selector/geometry/bounds cases and unsupported run/push/reverse/
water states reject atomically; task pause and translation teardown pass.

The authored-room trace exposed an original 3D pan lookup beyond the declared
68-element arrays. The user-owned SLUS_201.84 contains four zero padding bytes
following each table (ELF file offsets 0x22dbb0 and 0x22dc00). Native-only arrays
now bound all 72 reachable bins explicitly, retaining those retail bytes and the
unchanged original angle/volume formulas. Desktop/PS2 declarations stay at 68.
The distance-510 attenuation boundary remains gated.

Generate the private test reference from the ISO before linking the sound-pan
helper; the generated header is used only by that standalone test:

```powershell
python arm64/sound_pan_reference.py --iso '<your ISO>' --output arm64/build/retail_pan_reference.h
& arm64/Build-SoundPanCheck.ps1 -SkipBuild
```

The generator requires unique matches for both original 68-byte sequences and
verifies their trailing bytes. Quest comparison passes all 196,608 angle/distance
samples, covering every 16-bit listening direction and three distances, including
10,920 trailing-bin samples. The original source-joint footstep fixture now
admits every owned joint directly, without rotating the listener to avoid the
previously unsupported tail.

The isolated opening trace runs 3,000 simulated ticks, cancels the idle gesture
with forward input and completes 90 original walking updates in the authored
room. Original run input then reaches the explicit unported continuation guard.
This trace uses the original movie-skip input and does not establish complete
opening/campaign playability. No progression flags are forced. The installed
full opening remains behind its live-runtime readiness gate.

All six native suites and thirty-eight integration helpers pass on Quest 3.
Evidence: v80-suite-device.log and v80-<helper>-device.log in arm64/build.
v0.80 is installed (versionCode 80 / versionName 0.80). Startup reaches focused
OpenXR session state 5 without a fatal exception or native crash in the captured
v80-live-startup.log. The visible scene and full-opening readiness gate remain
unchanged.


## v0.81 - original running and source walk handoff

Native basic action 6 now calls the original bhCPM2_act_run controller. The
walking controller first requests its locked running state; the next tick maps
the incoming frame into the original run clip, sets the four-frame blend and
uses authored run speeds. Original health-scaled turning, running footstep
frames, release-forward stop and release-run transition back to walking retain
source order. The walk handoff keeps the original deferred movement and frame
remapping rather than switching clips early.

The player now retains six translation-block descriptors for walking/running
and all three health levels. Running validates every size word traversed by the
original bhGetTransZ getter, exact bank/block ownership, requested frame/speed
and current/health-selected pose clips. The original running callback reads its
speed before checking for release-forward; preflight validates that read even
when the input will stop movement. The walking-only validation API keeps its
three-block family bound. Both borrowed native bank views clear on teardown.

All locked-run directional masks reach the original callback. Unsupported
quick-turn and water states still reject before dispatch. Four water-effect
blocks are excluded only in native action builds, whose existing guards reject
those states; the desktop/PS2 controller bodies retain their original code.
Shared original player motion, body smoothing, footsteps, collision, world
joints and skin publication continue to run once per supported tick.

Quest checks pass 14,256 run/walk-handoff updates across two motion groups,
three health levels, eight headings and three steering patterns. Independent
speed/displacement/frame references, 13,248 settled disc-key poses, 1,200 source
footstep ticks and 5,246,208 world matrix channels pass (maximum matrix error
0.000033206). All sixteen directional masks, simultaneous turns, fifteen health
transitions with source frame/blend mapping and an owned synthetic wall's
independent actor/body clearance and stable sustained contact pass. Fourteen
malformed contexts reject atomically, including damaged speed headers on the
stop path, foreign tracks, invalid frames, health-selected clips, bounds and
unsupported quick-turn/water states. Task pause and bank teardown pass.

The isolated opening trace runs 3,000 simulated ticks, cancels idle, walks 90
updates, runs 90 and returns to walking for 12 updates in the authored room.
Reverse input then reaches the explicit unported movement guard. The trace uses
the original movie-skip path and does not establish complete opening/campaign
playability. No progression flags are forced. The installed full opening
remains behind its live-runtime readiness gate.

All six native suites and thirty-nine integration helpers pass on Quest 3.
Evidence: v81-suite-device.log and v81-<helper>-device.log in arm64/build.
v0.81 is installed (versionCode 81 / versionName 0.81). After headset wake,
startup reached focused OpenXR session state 5 without a fatal exception or
native crash in v81-live-startup.log. The later captured activity stop releases
the native window and shuts down the OpenXR instance normally. The visible
scene and full-opening readiness gate remain unchanged.

## v0.82 - original normal-actor initialization and blending

Enemy native motion tables now retain their decoded clip descriptors until room
teardown. The bounded actor guard validates bank/slot/header/track/key ownership,
frame and single-wrap step bounds, model mapping and blend state before the
first actor advances. It admits the original `init_subpl` / `em60_init` and
`move_subpl` / `bhEne_Event2` / `mv00_subpl0` paths for the supported generic
actor IDs. Their original controller bodies remain unchanged. Three original
motion-blending routines now use the existing desktop branch's explicitly
sequenced Euler-equivalence update on ARM64, avoiding the original unsequenced
pointer writes while preserving the intended joint transformation.

Both original opening actors (91 and 98) have empty native enemy-motion banks.
Their initialization and all 512 empty entries are checked against the original
no-body-motion behavior. A test-only fresh copy of RM_0000 points enemy banks at
its ten real room clips; it exercises retained bank provenance and actual disc
keys without modifying the ISO or installed room. The native helper verifies
24,496 initialization/continuing/reset cases, all eight blend counts, fast/slow
sampling, fractional/reverse/wrap clocks and 12,541,952 independent world matrix
channels (maximum error 0.000010286). Another 640 updates combine body blending,
all 20 facial expressions, original world evaluation and skin publication.
Twelve malformed second-actor contexts reject before batch advancement. Existing
held-body and scripted-motion checks still pass.

All six native suites and forty integration helpers pass on Quest 3. v0.82 is
installed (versionCode 82 / versionName 0.82), and startup reached focused
OpenXR state 5 without a fatal exception or native signal in the captured log.
The isolated 3,000-tick opening trace preserves idle, walking, running and the
source return to walking; reverse input still reaches its explicit unported
player-action guard. This is not a full opening or campaign acceptance result.

Stage 1 of the requested sequence remains in progress: hostile enemy dispatch,
perception, attacks, damage/death, contact and their original dependencies are
not completed by these generic actor callbacks. Player head/damage/interaction
integration, complete camera/effect/cutscene sequencing, opening/first-door/save
acceptance and full runtime enablement remain pending. Readiness stays gated.

## v0.83 - original enemy task and damage cleanup

The room actor phase now preflights the entire native enemy pool and calls the
original `bhControlEnemy` driver, instead of reproducing its transform and
callback loop in the host. The source retains pool-slot order (including holes),
`enow`, the player `ofx`/`ofz` reset, per-actor transform backup, `en_objn` reset
and hidden-object collection. Only its callback dispatch uses the bounded
native adapter: existing original facial control and supported `bhSubpl`
controllers. This does not admit hostile AI or linked-parent controllers.

The original `bhEne_InitDamage` now runs after each supported actor callback.
It clears the model's joint-damage entries, preserves the unused damage-array
tail, decrements nonzero combo timers and clears the original transient flags.
The unchanged function is linked from `zonzon1.c`; unused enemy/effect routines
are discarded. Native preflight validates registered player work, room mapping,
original task extent, supported controllers, model/face contexts, combo timer
underflow and the 16-entry object-list capacity before advancing an actor.
It rejects unowned, duplicate or linked contexts, including the original linked
condition that is evaluated before the active bit. Paused tasks preserve the
source player-offset reset while leaving actors, cursor and object list alone.

Quest checks cover 4,096 task updates with room-record order reversed relative
to pool slots, holes, active/hidden/dormant masks, five combo-timer values,
31 cleared damage entries with 33 preserved tail entries, source player-offset
semantics and ordered object collection with untouched list tails. All 524,288
independent world-matrix channels pass (maximum error 0.000004936). Thirteen
malformed contexts reject before actor/player/matrix/bookkeeping writes. Empty
and paused task semantics and rejected direct out-of-task callbacks also pass.

All six native suites and 41 integration helpers pass. The isolated opening
trace still runs 3,000 ticks, exits idle, walks, runs and returns to walking;
reverse input reaches the explicit unported action guard. Full opening,
control handoff, first transition and campaign save/load are not validated by
these isolated tests. Hostile perception, attacks, damage reactions and death
remain pending, together with the remaining player and presentation work.

v0.83 is installed (versionCode 83 / versionName 0.83). Startup reached
focused OpenXR state 5 with the headset awake and no fatal exception or native
signal in the captured app log. The visible scene remains unchanged.

## v0.84 - original zombie parent-work layout and reset

The parent zombie's original 0xb0-byte work contains nine adjacent four-byte
child pointers at offsets 0x00..0x20 and two collision pointers at 0x70/0x74.
Native pointer stores at those offsets would overlap neighboring fields.
`RxEnemy01Work` retains the original 176 scalar bytes and holds those pointers
in separate typed storage. The reserved pointer bytes in scalar work remain
zero; the shadow handle remains the original four bytes at 0x24. Bindings borrow
room-owned storage, reject overlap/foreign pool owners and detach on room
teardown. Callers must retire bindings before freeing the borrowed allocation.
The layout check validates child pool addresses, not child model/lifecycle or
collision-pointee ownership; controller admission still needs those checks.

The original reinitialization block is extracted from `bhEne01_Init` into
`bhEne01_ResetWork` and shared with the original initializer. It preserves seven
child links and the shadow handle, clears the scalar work, and clears the other
two child links and collision cache pointers. Parent pointer accesses in en01.c
and the parent-head lookup in en01b.c use typed accessors; the original raw
BH_PWORK +0x2ec parent load uses its named lkwkp field. Desktop/PS2 accessors
retain the original four-byte offsets. Linked body-part flag pointers in their
own work blocks remain unported.

The native build deliberately compiles only the extracted original reset body
from en01.c using RECVX_NATIVE_ENEMY01_WORK_ONLY. It does not compile or admit
the unfinished Init/MainLoop paths. The current supported actor driver and
installed opening gate are unchanged. Fresh work allocation and linked actor
construction must be integrated before the original hostile initialization can
run; later behavior/collision/effect dependencies also remain pending.

Quest checks pass 16,384 original reset calls and 180,224 native pointer round
trips above 4 GiB. Every scalar byte, seven preserved links, shadow bit pattern,
cleared tails and independent actor context match the reference rules. All
2,026 invalid slot selections, 44 reserved pointer bytes, nine foreign child
references, mismatched owners, short/misaligned/null/shared/overlapping or
overflowing bindings reject without partial changes. Adjacent independent
owners, retirement with a foreign replacement and actual room teardown pass.
All six native suites and 42 integration helpers pass. The isolated opening
still reaches the explicit unported reverse-action guard after its original
idle/walk/run/walk sequence; full opening and campaign acceptance remain pending.

v0.84 is installed (versionCode 84 / versionName 0.84). After headset/controller
wake, startup reached focused OpenXR state 5 without a fatal exception or native
signal in the captured app log. The visible scene remains unchanged.

## v0.85 - original enemy allocation and linked construction

`bhEne_CallocWork` now uses checked uintptr_t alignment on ARM64. A registered
native game-arena extent bounds the preview before its original cursor advance
and byte-zeroing loop execute. Native allocation uses that host arena's end
instead of the old hard-coded guest budget; desktop/PS2 paths are unchanged.
All 64 alignment residues, zero-size allocations, exact-end requests, padding,
negative sizes, overflow and corrupted/retired arena references are checked.
Arena registration follows opening allocation/free and survives room changes.

The native zombie work allocator preflights an unused work binding, typed storage,
capacity and overlap before calling original bhEne_CallocWork for the complete
native layout. Existing work is preserved, with the separate original reset
path retaining the original seven links and shadow handle. The source fresh
allocation branch has the native allocator hook, but the full Init body remains
excluded until its remaining dependencies are ported.

The original zob_lkmtab, bhEne01_SetLinkEnemy and bhSetEnemy constructor chain now
build for ARM64. Only preflight, native ownership registration and the explicit
retail return value are added around the source constructor. The native guard
checks bound parent work, a trusted loader's borrowed parent model/motion view,
joint range, supported linked IDs, original template identity, finite floor
heights, pool extent/capacity and stale first-free slot contexts. It preserves
the source first-free selection, parent pointer, joint, model/motion alias,
default transform/scale fields and pool-extent update. Parent model bindings
must outlive their links; room teardown retires the metadata before models go
away. Post-initialization part model changes, mid-room recycling and body-part
flag-pointer work still need their own source transitions and guards.

Quest checks pass 65,536 original allocator updates with byte-exact zeroing and
untouched padding/tails above 4 GiB, plus 126 native arena-backed zombie-work
allocations. All 1,116 original constructor cases pass across nine linked IDs,
every joint in the native opening-model fixture and four sparse free slots.
The fixture borrows an existing room model descriptor; it does not validate
zombie animation or live campaign enemies. A combined secondary-body/nine-part
fixture verifies original construction, typed pointer publication, preservation
on repeated allocation and existing-work reset. Twenty malformed/full/stale
spawns reject without changing the pool, extent or heap cursor; six altered
child identities reject. Arena/parent/link teardown also passes.

All six native suites and 43 integration helpers pass. The isolated opening
trace still runs 3,000 ticks, leaves idle, walks, runs and returns to walking,
then reaches the unported reverse-action guard. Complete opening, player
handoff, first transition and campaign save/load acceptance are still pending.
The native subset now includes the original reset, link template and linked
constructor; full hostile Init/MainLoop and linked part controllers stay gated.

v0.85 is installed (versionCode 85 / versionName 0.85). After headset/controller
wake, startup reached focused OpenXR state 5 without a fatal exception or native
signal in the captured app log. The visible scene is unchanged.

## v0.86 - original linked-part initialization and lifetime

The original bhEne01Head_Init, bhEne01Arm_Init, bhEne01Leg_Init and linked-state
bhEne01Cap_Init now compile and execute on ARM64. Native compilation exposes
only these initializer bodies from en01sub.c. Their original scalar writes,
mode changes, flag updates and fresh/existing allocation branches are retained;
only allocation and pointer accesses route through the bounded native adapter.
The unlinked cap branch, movement/damage controllers and other linked types are
not admitted by this wrapper.

Head scalar work remains 32 bytes; arm/leg/cap work remains 112 bytes, including
the original matrix area at byte 48. Adjacent pointer words 0/4 stay zero in that
scalar storage. Typed native object/world references live in per-pool-slot
metadata. The native source accessors and head-flag publication site use those
references; static assertions retain the original evalflags/flg-at-offset-zero
aliases. Initialization clears the native references as the original clears its
pointer words, without changing matrix/tail bytes in existing work.

Before source initialization, the wrapper validates the original linked owner,
supported type/state, scalar view and fresh allocation capacity/overlap. Existing
pointer pairs must address corresponding nodes within the registered borrowed
parent model, with type-specific minimum spans. Cross-kind overlap checks prevent
parent and part work from overwriting each other. This is an initialization and
ownership contract, not admission of detached motion, damage or geometry edits.

Inactive still-linked parts can retire once their parent work/secondary-body
reference and dependent contexts no longer reference them. Retirement detaches
matching owned work and link metadata without moving the arena cursor or changing
old buffer bytes. The original constructor can then reuse the first free slot;
new work still uses original bump allocation. Active, referenced, foreign-work,
detached or changed-alias contexts reject. Room teardown retires part metadata
before linked models and the game arena go away.

Quest checks pass 4,096 original initializer cases across all four supported
classes and fresh/existing work. Independent byte/actor checks cover exact scalar,
mode and flag changes, native pointer clearing, unchanged parent/model data and
retained matrix/tail bytes. Another 1,892 owned node slices above 4 GiB pass
without overlapping scalar work; 84 malformed slice/pointer/reserved-cell cases
reject before source writes. Exhausted/overlapping allocation, mismatched work,
unported continuation and owner identity guards pass. Four inactive part
retire/recycle/reinitialize sequences preserve old buffers and use new arena
allocations. These use a borrowed native opening-model fixture; live zombie
animation and authored campaign encounters are not established by them.

All six native suites and 44 integration helpers pass. The isolated opening
trace retains the original 3,000-tick idle/walk/run/walk sequence, then reaches
the explicit unported reverse-action guard. Full opening, player handoff, first
transition and campaign save/load acceptance remain pending. Native part work
and its original initializers are ready for the next bounded controller step;
they are not yet dispatched as a complete hostile enemy runtime.

v0.86 is installed (versionCode 86 / versionName 0.86). After headset wake,
the app activity is focused and captured Touch/player movement updates confirm
execution of the focused OpenXR path. Initial numeric session-state messages
had rolled out of the log buffer; no fatal exception or native signal appears
in the captured app log. The visible scene remains unchanged.

## Inventory entry call and arbitration (unreleased)

The inventory-request condition and selected branch of original bhCheckSubTask
are extracted as bhInventoryRequestCheck and bhBeginInventoryTask. The original
task arbiter uses the same shared routines, preserving its surrounding branch
order. Native wrappers validate owned player work and fade channels before the
transition. This ports the request and handoff components; it does not replace
or declare the original status UI complete.

Source eligibility retains scenario/event/fade locks, player incapacitation,
script-control exceptions, script-forced requests, aim suppression and PauseBtn.
Typewriter and computer requests retain higher priority. The source handoff
calls AllStopEnemySe, selects packed menu mode 5, sets status bit 8, pauses tasks
with sp_flg=0x30, requests the original black fade with parameter 5, and clears
BackColorFlag. Active/fading requests cannot enter twice. Other player/system
work is preserved.

Quest checks pass 4,194,304 flag/input/priority combinations, real original pad
conversion through all four control tables, twelve manual/script transitions,
duplicate-entry suppression and malformed player/fade/output rejection. Types
A-C produce the original inventory request from Start/R3 in the current raw-pad
adapter. The special Type D table also sets aim for its manual status input;
the original suppression rule is preserved rather than changing that table.
Script-forced requests retain their original exceptions.

All six native suites and 45 integration helpers pass. No new APK was installed:
calling the live transition before original ItemTaskCheck/StatusMain, native
S_WORK/resource ownership and status rendering are ready would pause/fade the
game without a usable menu. The frontend therefore continues to gate live menu
opening. Inventory is now the requested focus; linked-enemy work remains queued.

## Original inventory status initialization (unreleased)

`inventory_status.c` owns native source S_WORK/SITEM objects and preflights the
original ItemTaskCheck entry body, StatusInit normal branch and PulseInit. The
full source continues to use the same extracted entry/normal initialization
helpers. Pointer-bearing work is never copied from a retail 32-bit struct.
Native C sprite tables retain their original bounds and signed -1 sentinels.
The adapter validates their identities/sentinels before the source loops.

`Build-InventoryStatusCheck.ps1` builds the isolated device check. Quest passes
8,640 original initializations across four player banks, sixteen cursors, nine
health values, three poison states and five box-pointer contexts, alternating
normal/re-entry modes. It checks source screen/cursor flags, portrait selection,
health pulse colors/positions and the original zeroed 128 KiB allocation above
4 GiB, preserving item quantities. Malformed signed IDs, cursors, texture roots,
sprite roots/sentinels, checkpoints, arena bounds and duplicate initialization
reject before source writes. New Game and teardown reset native inventory views.
The isolated cold-entry fixture resets its unconsumed sound queue between cases.

This is initializer coverage, not a full StatusMain or item-box task port.
The initializer also supports an empty owned texture list for isolated checks;
the resource component below registers the original atlases. No status screen
is drawn yet. The remaining case-1 preface, drawing, actions and original
close/player-motion/room-texture restoration must work before the live
inventory call is enabled. No new APK is installed for this component.

## Original inventory resources and shared setup (unreleased)

The original disc-selected ITEM1/ITEM2 archive is now mounted as partition 4.
Member 145 contains five status atlases. The checked resource adapter owns a
mutable copy, bounds the PVP/image/palette stream and preflights texture slots
and heap space before original SbsTextureInit/PushPalletBuffer registration.
It admits the supported retail atlas dimensions/formats and base/alternate
palette ordering, preserving the source archive bytes. Native palette mode
set/get now has defined state instead of the empty PS2 stubs. Reset releases
owned atlas references, compacts the native texture heap, restores the saved
palette through original PopPalletBuffer and retires the source view before
the game texture heap is destroyed. This is resource lifetime support, not
the original gameplay close/restore task.

The shared StatusMain case-1 tail now runs original CenterPositionInit,
CursorInit, bhCheckSubPack and BgColorInit, retaining cursor state, event-driven
capacity flags, layout dimensions and workspace checkpoint. Original item.c
metadata tables are linked directly. Native CursorInit uses the typed six-byte
item record instead of stepping beyond the first record through a type-field
pointer. File-view work is native-owned; the full file-screen task is pending.

Quest passes the expanded 8,640 initialization/setup cases, cursor dispatch
for all 156 source item entries, four atlas register/retire/reload cycles and
all 1,088,048 truncated member prefixes without status/palette mutation.
Unsupported palette ordering, dimensions, exhausted texture slots/heap, bad
item metadata/cursor roots and duplicate setup reject. The source archive
remains immutable and texture-ring/palette restoration passes. All six native
suites and 46 freshly linked integration helpers pass. Logs are in
`build/inventory-resources-*-device.log`. No GPU status-screen acceptance is
claimed: the case-1 preface, per-frame drawing/actions and source gameplay
close/restore path remain gated. Installed APK stays at v0.86.

## Original inventory sprite transport (unreleased)

Original SpriteSet2D and njDrawSprite2D now compile directly for ARM64. The
inner sprite dispatch from StatusMain is shared as StatusDrawSprite, retaining
source palette selection, material, center offsets and horizontal/vertical
repeat stepping. A native single-sprite collector validates source table and
animation identities/extents, atlas selection, material and finite geometry
before dispatch. Hidden sprites preserve the original early skip. The native
primitive route captures the original four-vertex integer-UV triangle strip,
including its source depth and colors. Owned RGBA atlas/palette snapshots
allow repeated presentation after the source resource owner is retired.

The GLES transport preserves packet coordinates, UVs, GS material/alpha
modulation, point/linear filtering and texture wrap state. It invalidates the
shared particle/message texture cache after snapshot uploads. Original
SpriteH/SpriteUV execute behind normal-status transition guards: the eight-frame
center trajectory, completion handoff, five-step panel settling and six-step
crop opening are checked independently on Quest. This API handles the opening
display transition only, not menu commands or closing to gameplay.

Build-InventorySpriteCheck.ps1 links the isolated Quest test. All 120 source
table entries produce the expected 214 visible/repeated polygons; 72 interior
Quest GLES samples match independent atlas sampling/filtering/material/alpha
calculations within three channel values. Inventory work and source sprites
remain unchanged during collection. Malformed indices, animation roots and
nonfinite repetition reject without replacing an existing frame. Repeated
collection and full framebuffer replay are exact, including after atlas
retirement. All six native suites and 47 freshly linked integration helpers
pass; logs are in build/inventory-sprite-*-device.log.

This verifies individual original sprite dispatch and display motion, not
the complete status screen. The subsequent item-display component below adds
original icon/quantity/equipment construction and dynamic table bounds. The
remaining SpriteOnOff state/name display, full ordering/composition, health
pulse drawing, menu/action dispatch and original close/player-motion/room-texture
restoration remain pending. The current collector snapshots one source sprite
at a time; full-screen batching/cache ownership needs separate integration.
No new APK is installed and the live inventory call remains gated.

## Original item display construction (unreleased)

Original ItemSort, ItemSet, SidePackSet, KazuSet, BulletSet and ArmsSet now
compile on ARM64. StatusItemSprites shares the original display call sequence
with SpriteOnOff, whose remaining state/name/action paths are still excluded.
ItemSort now uses explicit 32-bit inventory words at +2 on the native path: its
old NJS_TEXNAME cast depended on the PS2 texaddr field being eight bytes past
the cast address. The original stable wide-item-first ordering and packed
flag updates remain in the original routine. Other platform branches retain
their original access. Native KazuSet also uses the existing PC empty-range
initialization for its inactive non-box probe, avoiding uninitialized bounds.

The checked normal-selection adapter validates bank ownership, equipped
cursor, item IDs, cell occupancy, percentage divisors/quantities, digit-atlas
limits and the complete quantity/equipped glyph budget before source writes.
The original 16-icon and 40-number C arrays remain fixed; their bounded live
sentinel positions are published only after original construction. Reset
clears these dynamic views before reuse. Unsupported item-box, combination
and menu modes remain gated. This component sorts inventory words as the
original does; it is not a read-only drawing call or an item-action runtime.

Build-InventoryItemsCheck.ps1 links the Quest check. All 19,968 fixtures
(each built twice) pass across four native banks above 4 GiB, all 156 item IDs,
eight/ten-cell capacities, four packed-flag policies and quantity boundaries.
Independent stable ordering and decimal/percentage/infinity formatting match;
header/personal/tail words and all other banks remain intact. Six original
icon alias/atlas fixtures pass. Invalid IDs/cursors, overfilled cells, invalid
quantities, excessive glyph totals and unsupported modes reject without item,
UV, sprite or ownership changes. A source-built mixed inventory submits 21
dynamic icon/equipment/quantity polygons successfully through Quest GLES. All
six native suites and 48 freshly linked integration helpers pass; logs are in
build/inventory-items-*-device.log. No complete-screen or gameplay-return
acceptance is claimed, and the installed APK remains v0.86.

## Original cursor and item-name display (unreleased)

Original CursorSet now compiles in the native subset. Its item type lookup
uses the typed six-byte metadata record, retaining original width/alignment,
position, pulse counters/colors and hidden-cursor behavior. Checked normal
selection rejects invalid physical/logical cursors, item IDs, pulse state,
foreign animation roots and unported modes before source writes. This ports
cursor display, not CursorMove input navigation or action/menu dispatch.

New Game reads original SYSMES.ALD through GDFS and owns a native copy of its
first length-prefixed bank: 183 item-name entries on the supported US disc.
The system-message bank in the rest of that file is not published by this
component. Count/offset/extent/terminator and supported glyph checks precede
source reads, including revalidation of the published native pointer. The
name bank is retired on New Game reset/teardown. Shared original selection
and position helpers retain the Storage Key, Empty Extinguisher and Playing
Manual aliases, including the original low-byte quantity test.

Original bhDispItemName/bhDispFont produce a separate checked RxMessageFrame
without replacing or advancing the live dialogue frame. Geometry is copied;
the common font atlas remains borrowed from the current game lifetime. The
shared font presenter accepts these independent frames, and rejects an
expired texture-heap context before touching the retired font. Font scale,
geometry bounds and the common atlas are checked before collection. Existing
room-message opcode/readiness gates remain intact.

Build-InventoryNamesCheck.ps1 links the Quest check. All 49,920 cursor states
across four native banks, all 156 item IDs, capacities/positions and both cursor
variants match independent state/geometry/width references without changing
item words or unrelated work. All 183 disc names at three scales (549 layouts)
match independent glyph advances, spaces, UVs and colors; seven flag/quantity
alias cases and hidden selection pass. All 5,456 truncated first-bank prefixes,
foreign roots/offsets, unsupported glyphs and invalid cursor/name/scale cases
reject. Every name draws through Quest GLES under original inventory palettes
(445,967 lit pixels across the fixture); dialogue frames stay unchanged and
frozen geometry replays exactly after text-bank retirement while its font
remains owned. All six native suites and 49 freshly linked integration helpers
pass; logs are in build/inventory-names-*-device.log. This is display/backend
coverage, not a complete or interactive status screen. Installed v0.86 stays
unchanged and the live inventory call remains gated.

## Original navigation and top menu (unreleased)

CursorMove, MainCommand, StatusCancel and the original file-request metadata
initializer now compile directly in the native subset. Checked input dispatch
requires the owned normal status work, the most recent original ItemSort wide
boundary, a consistent physical/logical cell mapping and bounded repeat/menu
state. It calls the unchanged source navigation, button-priority, held-repeat
and confirmation-delay logic. Modes 4/8/32 remain pending item/file/closing
handoffs; this adapter does not execute item actions, file/map resources or
restore gameplay. StatusCancel here changes only its original flags/layout
targets. The live inventory button is still gated.

The second length-prefixed SYSMES.ALD bank now has separate native ownership
and offset validation (494 entries / 83,624 bytes in the local US disc). Its
opcode content is validated by each reader, not accepted wholesale. The
original top-menu help entries 177-180 and 199 must pass the existing bounded
message scanner before MainCommand can read their table. Message ticking can
select an owned system-bank page while preserving the original room-only
event message checks; unsupported controls and foreign/stale pages still
reject. Both text-bank roots retire with the game.

Build-InventoryNavigationCheck.ps1 passes on the reconnected Quest: 767,232
source cursor states match an independent grid/repeat/button-priority
reference, preserving item words and exact source handoff/cancel targets. All
3,456 top-menu navigation/repeat cases, confirmation-delay handoffs across four
game modes and direction input through all four original pad tables pass.
Malformed cursor/sort-boundary/phase/system-bank states reject before menu
writes. The separately owned 494-entry system bank passes 89,084 truncated
prefix rejections; all five original menu-help messages pass through the
original message controller and Quest GLES alongside matching source header
sprites. These are backend/offscreen checks, not in-headset interactive-menu
acceptance. All six native suites and 50 freshly linked integration helpers
pass. Logs are in build/inventory-navigation-*-device.log and build logs in
build/inventory-navigation-*-build.log. Installed v0.86 and all live inventory
/full-runtime gates remain unchanged.
