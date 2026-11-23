cmake_minimum_required(VERSION 3.20)
project(recvx_android_arm64 LANGUAGES C)

if(NOT ANDROID OR NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Configure with the Android NDK and -DANDROID_ABI=arm64-v8a")
endif()

# Exercise the same DMA walker and portable VU core as the original engine.
# This target is intentionally independent of the desktop host and Quest VR app.
set(engine "${CMAKE_CURRENT_LIST_DIR}/../port/pc")
set(decomp "${engine}/../decomp")
add_library(recvx_egl STATIC android_egl.c)
set_target_properties(recvx_egl PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(recvx_egl PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_compile_options(recvx_egl PRIVATE -Wall -Wextra -Werror)
target_link_libraries(recvx_egl PUBLIC EGL android)

set(boot_dependencies)
foreach(unit IN ITEMS gdlib ps2_sg_gd ps2_dvd_image ps2_sg_maloc adxwrap adv ps2_sg_pad
        ps2_Ninjapad padman pad player main sdc sdfunc vibman system expand)
    list(APPEND boot_dependencies "${decomp}/src/ps2/veronica/prog/${unit}.c")
endforeach()
add_library(recvx_android SHARED android/native_activity.c android/disc.c android/boot_disc.c
    event_movie.c
    android/title_draw.c android/title_menu.c android/pad_input.c android/opening.c android/opening_draw.c
    android/movie_port.c android/movie_draw.c
    "${engine}/src/ps2/cdvd_image.c" "${engine}/src/ps2/cdvd.c"
    "${engine}/src/audio/adx.c" "${engine}/src/movie/mpeg2.c" "${engine}/src/movie/pss.c" ${boot_dependencies})
set_source_files_properties(${boot_dependencies} PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-include;${engine}/include/pc_prelude.h")
target_compile_definitions(recvx_android PRIVATE RECVX_NATIVE_DISC RECVX_NATIVE_HEAP
    RECVX_NATIVE_TITLE_DRAW RECVX_NATIVE_PAD RECVX_NATIVE_OPENING RECVX_NATIVE_MOVIE
    RECVX_NATIVE_ROOM_SCRIPTS RECVX_NATIVE_ROOM RECVX_NATIVE_EVENT_CORE RECVX_VU_PORTABLE)
target_include_directories(recvx_android PRIVATE "${engine}/src" "${engine}/src/ps2" android)
target_include_directories(recvx_android SYSTEM PRIVATE
    "${engine}/include" "${engine}/include/sdk" "${decomp}/include/ps2/veronica/prog"
    "${decomp}/src/cri/mwlib/ee/lib/libadxe")
target_compile_options(recvx_android PRIVATE -Wall -Wextra -Werror -fsigned-char
    -fvisibility=hidden -ffunction-sections -fdata-sections
    "SHELL:-idirafter '${decomp}/include/recvx-decomp-katana/KATANA/Include'"
    "SHELL:-idirafter '${decomp}/include/recvx-decomp-cri/cri/mwlib/include'"
    "SHELL:-idirafter '${decomp}/include/recvx-decomp-cri/cri/mwlib/ee/include'")
target_link_options(recvx_android PRIVATE "-Wl,--gc-sections")
target_link_libraries(recvx_android PRIVATE recvx_egl GLESv3 aaudio log m)

set(openxr "${CMAKE_CURRENT_LIST_DIR}/third_party/openxr")
add_library(recvx_openxr_loader SHARED IMPORTED)
set_target_properties(recvx_openxr_loader PROPERTIES
    IMPORTED_LOCATION "${openxr}/lib/arm64-v8a/libopenxr_loader.so")
target_sources(recvx_android PRIVATE android/theater.c "${engine}/src/host/vr_math.c")
target_include_directories(recvx_android SYSTEM PRIVATE "${openxr}/include")
target_link_libraries(recvx_android PRIVATE recvx_openxr_loader)
configure_file("${openxr}/lib/arm64-v8a/libopenxr_loader.so"
    "${CMAKE_CURRENT_BINARY_DIR}/libopenxr_loader.so" COPYONLY)
set_source_files_properties(android/theater.c PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")

add_executable(recvx_check_ee test_memory_dma.c memory_bridge.c
    layout_probe.c "${engine}/src/ps2/guest_memory.c" "${engine}/src/ps2/dma.c")
target_include_directories(recvx_check_ee PRIVATE
    "${engine}/src" "${engine}/include" "${engine}/include/sdk")
target_compile_definitions(recvx_check_ee PRIVATE RECVX_TRANSLATED_MEMORY)
target_compile_options(recvx_check_ee PRIVATE -Wall -Wextra -Werror)
# Katana ships headers named stdarg.h, stdlib.h, etc. Prefer Android's libc.
target_compile_options(recvx_check_ee PRIVATE
    "SHELL:-idirafter '${engine}/../decomp/include/recvx-decomp-katana/KATANA/Include'")

add_executable(recvx_check_vu "${engine}/tests/test_vu.c" "${engine}/src/ps2/vu.c")
target_compile_definitions(recvx_check_vu PRIVATE RECVX_VU_PORTABLE)
target_compile_options(recvx_check_vu PRIVATE
    -ffp-contract=off -fno-strict-aliasing -fwrapv -fsigned-char)
target_link_libraries(recvx_check_vu PRIVATE m)

add_executable(recvx_check_opening test_opening.c ninja_binary.c)
target_compile_options(recvx_check_opening PRIVATE -Wall -Wextra -Werror)
target_link_libraries(recvx_check_opening PRIVATE m)

set(original_cut "${decomp}/src/ps2/veronica/prog/cut.c")
set(original_event "${decomp}/src/ps2/veronica/prog/event.c")
set(original_motion "${decomp}/src/ps2/veronica/prog/Motion.c")
set(original_room "${decomp}/src/ps2/veronica/prog/room.c")
add_executable(recvx_check_events test_event_runtime.c event_runtime.c room_scripts.c room_header.c
    "${engine}/src/ps2/vu.c"
    "${original_motion}" "${decomp}/src/ps2/veronica/prog/MdlPut.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaMatrix.c" "${decomp}/src/ps2/veronica/prog/ps2_NaMath.c"
    "${original_event}" "${decomp}/src/ps2/veronica/prog/effect.c"
    "${decomp}/src/ps2/veronica/prog/njplus.c" "${decomp}/src/ps2/veronica/prog/pwksub.c")
target_compile_definitions(recvx_check_events PRIVATE RECVX_NATIVE_ROOM RECVX_NATIVE_ROOM_SCRIPTS RECVX_NATIVE_EVENT_CORE RECVX_VU_PORTABLE)
target_include_directories(recvx_check_events PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${engine}/src" "${engine}/src/ps2")
target_include_directories(recvx_check_events SYSTEM PRIVATE
    "${engine}/include" "${engine}/include/sdk" "${decomp}/include/ps2/veronica/prog"
    "${decomp}/src/cri/mwlib/ee/lib/libadxe")
target_compile_options(recvx_check_events PRIVATE -Wall -Wextra -Werror -fsigned-char
    "SHELL:-idirafter '${decomp}/include/recvx-decomp-katana/KATANA/Include'"
    "SHELL:-idirafter '${decomp}/include/recvx-decomp-cri/cri/mwlib/include'"
    "SHELL:-idirafter '${decomp}/include/recvx-decomp-cri/cri/mwlib/ee/include'")
target_link_options(recvx_check_events PRIVATE "-Wl,--gc-sections")
target_link_libraries(recvx_check_events PRIVATE m)

set(native_sound event_audio.c "${engine}/src/audio/snd_synth.c"
    "${decomp}/src/ps2/veronica/prog/ps2_sg_sd.c"
    "${decomp}/src/ps2/veronica/prog/ps2_snddrv.c")
target_sources(recvx_android PRIVATE ${native_sound})
target_compile_definitions(recvx_android PRIVATE RECVX_NATIVE_SOUND)
target_sources(recvx_check_events PRIVATE ${native_sound} test_event_audio.c test_sound_requests.c test_face_controls.c
    "${decomp}/src/ps2/veronica/prog/sdc.c" "${decomp}/src/ps2/veronica/prog/sdfunc.c")
target_compile_definitions(recvx_check_events PRIVATE RECVX_NATIVE_SOUND)
target_link_libraries(recvx_check_events PRIVATE aaudio)
set_source_files_properties(${native_sound} PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-include;${CMAKE_CURRENT_LIST_DIR}/sound_declarations.h")

add_executable(recvx_check_effects test_effects.c room_engine.c ninja_binary.c)
target_compile_definitions(recvx_check_effects PRIVATE RECVX_NATIVE_ROOM_SCRIPTS RECVX_NATIVE_ROOM)
get_target_property(effect_options recvx_check_events COMPILE_OPTIONS)
target_include_directories(recvx_check_effects PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${engine}/src")
target_include_directories(recvx_check_effects SYSTEM PRIVATE
    "${engine}/include" "${engine}/include/sdk" "${decomp}/include/ps2/veronica/prog"
    "${decomp}/src/cri/mwlib/ee/lib/libadxe")
target_compile_options(recvx_check_effects PRIVATE ${effect_options} -ffunction-sections -fdata-sections)
target_link_options(recvx_check_effects PRIVATE "-Wl,--gc-sections")
target_link_libraries(recvx_check_effects PRIVATE m)
set(room_dependencies)
set_source_files_properties("${decomp}/src/ps2/veronica/prog/game.c" PROPERTIES COMPILE_OPTIONS
    "-DRECVX_NATIVE_GAME_FLAGS;-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off")
foreach(unit IN ITEMS eneset objitm njplus pwksub flag dread ps2_NaTextureFunction ps2_texture ps2_dummy ps2_loadtim2 ps2_pxlconv ps2_NaMatrix ps2_NaMath ps2_NaSystem ps2_NaView ps2_NaFog MdlPut camera light sync ps2_NaDraw2D ps2_NinjaCnk ps2_Vu1Scissor2)
    list(APPEND room_dependencies "${decomp}/src/ps2/veronica/prog/${unit}.c")
endforeach()
target_sources(recvx_android PRIVATE room_header.c room_binary.c room_engine.c room_draw.c room_scripts.c
    game_runtime.c "${decomp}/src/ps2/veronica/prog/game.c"
    room_skin.c ninja_binary.c event_runtime.c shadow_runtime.c effect_runtime.c effect_callbacks.c room_effects.c
    android/shadow_draw.c android/particle_draw.c "${original_event}"
    "${decomp}/src/ps2/veronica/prog/effect.c" "${decomp}/src/ps2/veronica/prog/effsub1.c"
    "${original_cut}" "${original_room}" "${original_motion}" ${room_dependencies}
    "${decomp}/src/ps2/veronica/prog/hitchk.c" "${decomp}/src/ps2/veronica/prog/ps2_NaColi.c"
    "${decomp}/src/ps2/veronica/prog/weapon.c"
    "${engine}/src/ps2/vu.c" "${engine}/src/gs/gs_mem.c" "${engine}/src/gs/gs.c")
add_executable(recvx_check_room test_room_binary.c test_texture_heap.c room_header.c room_binary.c
    test_shadow_runtime.c test_room_effects.c test_particle_runtime.c test_overlay_runtime.c test_cinema_runtime.c test_filter_runtime.c room_effects.c shadow_runtime.c effect_runtime.c effect_callbacks.c
    android/shadow_draw.c android/particle_draw.c
    "${decomp}/src/ps2/veronica/prog/effect.c" "${decomp}/src/ps2/veronica/prog/effsub1.c"
    "${decomp}/src/ps2/veronica/prog/hitchk.c" "${decomp}/src/ps2/veronica/prog/ps2_NaColi.c"
    test_texture_gpu.c "${engine}/src/gs/gs_mem.c" "${engine}/src/gs/gs.c"
    test_room_matrix.c "${engine}/src/ps2/vu.c"
    room_engine.c room_draw.c room_scripts.c room_skin.c ninja_binary.c "${original_cut}" "${original_event}" "${original_motion}"
    "${original_room}" ${room_dependencies})
# Link only the original engine functions exercised by the harness.
set_source_files_properties("${original_cut}" "${original_event}" "${original_motion}"
    "${decomp}/src/ps2/veronica/prog/effect.c" "${decomp}/src/ps2/veronica/prog/effsub1.c"
    "${decomp}/src/ps2/veronica/prog/weapon.c"
    "${decomp}/src/ps2/veronica/prog/hitchk.c" "${decomp}/src/ps2/veronica/prog/ps2_NaColi.c"
    "${original_room}" ${room_dependencies} PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off")
set_property(SOURCE "${original_event}" "${decomp}/src/ps2/veronica/prog/objitm.c"
    "${decomp}/src/ps2/veronica/prog/effect.c" "${decomp}/src/ps2/veronica/prog/effsub1.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaTextureFunction.c" "${decomp}/src/ps2/veronica/prog/ps2_dummy.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaMatrix.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NinjaCnk.c"
    APPEND PROPERTY COMPILE_OPTIONS "-Wno-return-type")
set_property(SOURCE "${decomp}/src/ps2/veronica/prog/objitm.c" "${decomp}/src/ps2/veronica/prog/njplus.c"
    "${decomp}/src/ps2/veronica/prog/ps2_dummy.c" "${decomp}/src/ps2/veronica/prog/ps2_loadtim2.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaMatrix.c" "${decomp}/src/ps2/veronica/prog/ps2_NaMath.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NinjaCnk.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaSystem.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaDraw2D.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaView.c"
    "${decomp}/src/ps2/veronica/prog/ps2_Vu1Scissor2.c"
    APPEND PROPERTY COMPILE_OPTIONS -DRECVX_VU_PORTABLE -include "${engine}/include/pc_prelude.h")
target_compile_definitions(recvx_check_room PRIVATE RECVX_NATIVE_ROOM_SCRIPTS RECVX_NATIVE_ROOM)
set_source_files_properties("${engine}/src/gs/gs.c" "${engine}/src/gs/gs_mem.c"
    PROPERTIES COMPILE_OPTIONS "-ffunction-sections;-fdata-sections")
set_source_files_properties("${engine}/src/ps2/vu.c"
    PROPERTIES COMPILE_OPTIONS "-ffunction-sections;-fdata-sections;-ffp-contract=off;-fwrapv;-fno-strict-aliasing")
target_compile_definitions(recvx_check_room PRIVATE RECVX_VU_PORTABLE)
target_link_options(recvx_check_room PRIVATE "-Wl,--gc-sections")
target_include_directories(recvx_check_room PRIVATE "${engine}/src" "${engine}/src/ps2" "${CMAKE_CURRENT_LIST_DIR}")
target_include_directories(recvx_check_room SYSTEM PRIVATE
    "${engine}/include" "${engine}/include/sdk"
    "${decomp}/include/ps2/veronica/prog" "${decomp}/src/cri/mwlib/ee/lib/libadxe")
target_compile_options(recvx_check_room PRIVATE -Wall -Wextra -Werror -fsigned-char
    "SHELL:-idirafter '${decomp}/include/recvx-decomp-katana/KATANA/Include'"
    "SHELL:-idirafter '${decomp}/include/recvx-decomp-cri/cri/mwlib/include'"
    "SHELL:-idirafter '${decomp}/include/recvx-decomp-cri/cri/mwlib/ee/include'")
target_link_libraries(recvx_check_room PRIVATE m recvx_egl GLESv3)

foreach(target IN ITEMS recvx_check_ee recvx_check_vu recvx_check_opening recvx_check_room recvx_check_events recvx_check_effects recvx_android)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    target_link_options(${target} PRIVATE "-Wl,-z,max-page-size=16384")
endforeach()

# Original common-control angle arithmetic uses signed 32-bit wrapping.
foreach(target IN ITEMS recvx_android recvx_check_room recvx_check_events)
    target_sources(${target} PRIVATE random_runtime.c)
    target_sources(${target} PRIVATE face_control_runtime.c)
    target_compile_definitions(${target} PRIVATE RECVX_NATIVE_RANDOM)
    target_compile_options(${target} PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/random_runtime.h")
endforeach()
set_property(SOURCE "${original_event}" APPEND PROPERTY COMPILE_OPTIONS -fwrapv)

# Retain original native facial bodies; legacy PS2 pointer fixups stay excluded.
set(original_face "${decomp}/src/ps2/veronica/prog/face_bh.c")
set(original_memory "${decomp}/src/ps2/veronica/prog/ps2_NaMem.c")
set_source_files_properties(${original_face} ${original_memory} PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off;-fno-strict-aliasing;-fwrapv;-DRECVX_NATIVE_FACE_SAMPLING;-include;${engine}/include/pc_prelude.h")
foreach(target IN ITEMS recvx_android recvx_check_room recvx_check_events recvx_check_effects)
    target_sources(${target} PRIVATE face_sampling.c face_mesh.c face_pose.c "${decomp}/src/ps2/veronica/prog/face.c" ${original_face} ${original_memory})
endforeach()
set_source_files_properties("${decomp}/src/ps2/veronica/prog/face.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off;-fwrapv;-DRECVX_NATIVE_FACE;-include;${engine}/include/pc_prelude.h")
set_source_files_properties(face_pose.c PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
target_sources(recvx_check_events PRIVATE test_face_sampling.c)

target_sources(recvx_check_room PRIVATE test_face_mesh.c test_face_jaw.c test_face_aux.c test_face_pose.c)
set_source_files_properties(test_face_jaw.c test_face_aux.c test_face_pose.c PROPERTIES COMPILE_OPTIONS -ffp-contract=off)

# CamSet retains its original camera/cut/background and clip-table consumers.
foreach(target IN ITEMS recvx_android recvx_check_room recvx_check_events)
    target_sources(${target} PRIVATE "${decomp}/src/ps2/veronica/prog/ps2_event.c")
endforeach()
set_source_files_properties("${decomp}/src/ps2/veronica/prog/ps2_event.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-include;${engine}/include/pc_prelude.h")
target_sources(recvx_check_events PRIVATE "${original_cut}"
    "${decomp}/src/ps2/veronica/prog/camera.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaView.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaSystem.c"
    "${decomp}/src/ps2/veronica/prog/flag.c"
    "${decomp}/src/ps2/veronica/prog/ps2_Vu1Scissor2.c")

foreach(target IN ITEMS recvx_android recvx_check_room recvx_check_events)
    target_sources(${target} PRIVATE "${decomp}/src/ps2/veronica/prog/ps2_Vu1Strip.c" "${engine}/src/ps2/libvu0.c")
endforeach()
set_source_files_properties("${decomp}/src/ps2/veronica/prog/ps2_Vu1Strip.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-DRECVX_NATIVE_CAMERA;-DRECVX_VU_PORTABLE;-include;${engine}/include/pc_prelude.h")
set_source_files_properties("${engine}/src/ps2/libvu0.c" PROPERTIES COMPILE_OPTIONS "-ffunction-sections;-fdata-sections")
target_sources(recvx_check_events PRIVATE "${decomp}/src/ps2/veronica/prog/ps2_dummy.c" "${decomp}/src/ps2/veronica/prog/ps2_NaDraw2D.c")

foreach(target IN ITEMS recvx_android recvx_check_events)
    target_sources(${target} PRIVATE adx_native.c)
endforeach()
target_sources(recvx_check_events PRIVATE "${decomp}/src/ps2/veronica/prog/adxwrap.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaColi.c"
    "${engine}/src/audio/adx.c" "${engine}/src/ps2/cdvd.c" "${engine}/src/ps2/cdvd_image.c")
target_compile_definitions(recvx_check_events PRIVATE RECVX_NATIVE_DISC)

set(original_adx_decode
    "${decomp}/src/cri/mwlib/ee/lib/libadxe/adx_dcd.c"
    "${decomp}/src/cri/mwlib/ee/lib/libadxe/adx_dcd3.c"
    "${decomp}/src/cri/mwlib/ee/lib/libadxe/adx_qtbl.c")
set_source_files_properties(${original_adx_decode} PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off;-fno-strict-aliasing;-fwrapv;-include;${engine}/include/pc_prelude.h")
foreach(target IN ITEMS recvx_android recvx_check_events)
    target_sources(${target} PRIVATE ${original_adx_decode})
endforeach()

target_sources(recvx_check_events PRIVATE test_frame_check.c)

foreach(target IN ITEMS recvx_android recvx_check_events)
    target_sources(${target} PRIVATE player_control_runtime.c)
endforeach()

target_sources(recvx_check_events PRIVATE test_player_controls.c)

target_sources(recvx_check_events PRIVATE room_engine.c)

# Reuse the original standing-motion routine and table in isolated event checks.
target_sources(recvx_check_events PRIVATE "${decomp}/src/ps2/veronica/prog/player.c")
set_property(SOURCE "${decomp}/src/ps2/veronica/prog/player.c" APPEND PROPERTY COMPILE_OPTIONS
    -ffunction-sections -fdata-sections -fno-strict-aliasing)

target_sources(recvx_check_events PRIVATE "${original_room}" "${decomp}/src/ps2/veronica/prog/light.c")

target_sources(recvx_check_events PRIVATE "${decomp}/src/ps2/veronica/prog/ps2_NinjaCnk.c"
    "${decomp}/src/ps2/veronica/prog/ps2_NaGraphics3D.c")
set_source_files_properties("${decomp}/src/ps2/veronica/prog/ps2_NaGraphics3D.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-DRECVX_VU_PORTABLE;-include;${engine}/include/pc_prelude.h")

foreach(target IN ITEMS recvx_android recvx_check_events)
    target_sources(${target} PRIVATE event_midi_bank.c)
endforeach()

foreach(target IN ITEMS recvx_android recvx_check_events recvx_check_room)
    target_sources(${target} PRIVATE message_runtime.c "${decomp}/src/ps2/veronica/prog/message.c")
endforeach()
set_source_files_properties("${decomp}/src/ps2/veronica/prog/message.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off;-include;${engine}/include/pc_prelude.h")

foreach(target IN ITEMS recvx_android recvx_check_events recvx_check_room)
    target_sources(${target} PRIVATE fade_runtime.c "${decomp}/src/ps2/veronica/prog/screen.c")
endforeach()
set_source_files_properties("${decomp}/src/ps2/veronica/prog/screen.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off;-include;${engine}/include/pc_prelude.h")

foreach(target IN ITEMS recvx_android recvx_check_events recvx_check_room)
    target_sources(${target} PRIVATE event_effects.c)
endforeach()

# Retain the original cleanup passes without compiling unported effect families.
set(original_effect_clear "${decomp}/src/ps2/veronica/prog/effsub2.c"
    "${decomp}/src/ps2/veronica/prog/effsub3.c")
foreach(target IN ITEMS recvx_android recvx_check_events recvx_check_room)
    target_sources(${target} PRIVATE ${original_effect_clear})
endforeach()
set_source_files_properties(${original_effect_clear} PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-DRECVX_NATIVE_EVENT_CLEAR;-include;${engine}/include/pc_prelude.h")

foreach(target IN ITEMS recvx_android recvx_check_events recvx_check_room)
    target_sources(${target} PRIVATE "${decomp}/src/ps2/veronica/prog/subpl.c")
endforeach()
set_source_files_properties("${decomp}/src/ps2/veronica/prog/subpl.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off;-fwrapv;-include;${engine}/include/pc_prelude.h")

foreach(target IN ITEMS recvx_android recvx_check_events)
    target_sources(${target} PRIVATE "${decomp}/src/ps2/veronica/prog/pl_evt.c")
endforeach()
set_source_files_properties("${decomp}/src/ps2/veronica/prog/pl_evt.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off;-fwrapv;-fno-strict-aliasing;-DRECVX_NATIVE_SCRIPT_PLAYER;-include;${engine}/include/pc_prelude.h")

set_property(SOURCE "${decomp}/src/ps2/veronica/prog/player.c" APPEND PROPERTY COMPILE_OPTIONS
    -DRECVX_NATIVE_PLAYER_ACTION -fwrapv -ffp-contract=off)

# Retain original enemy per-frame damage cleanup in the bounded native task.
foreach(target IN ITEMS recvx_android recvx_check_room recvx_check_events)
    target_sources(${target} PRIVATE "${decomp}/src/ps2/veronica/prog/zonzon1.c")
endforeach()
set_source_files_properties("${decomp}/src/ps2/veronica/prog/zonzon1.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off")

# Original zombie work reset and linked constructor; hostile controllers stay gated.
foreach(target IN ITEMS recvx_android recvx_check_room recvx_check_events)
    target_sources(${target} PRIVATE enemy01_work.c enemy01_links.c "${decomp}/src/ps2/veronica/prog/en01.c")
endforeach()
set_source_files_properties("${decomp}/src/ps2/veronica/prog/en01.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off;-fwrapv;-fno-strict-aliasing;-DRECVX_NATIVE_ENEMY01_WORK_ONLY")

# Original initializers for bounded head/arm/leg/cap work; damage stays gated.
foreach(target IN ITEMS recvx_android recvx_check_room recvx_check_events)
    target_sources(${target} PRIVATE enemy01_parts.c "${decomp}/src/ps2/veronica/prog/en01sub.c")
endforeach()
set_source_files_properties("${decomp}/src/ps2/veronica/prog/en01sub.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off;-fwrapv;-fno-strict-aliasing;-DRECVX_NATIVE_ENEMY01_PART_INIT_ONLY")

# Original inventory request/transition; status rendering is not yet admitted.
target_sources(recvx_android PRIVATE inventory_runtime.c)
target_sources(recvx_android PRIVATE item_names.c)

# Original inventory tables and entry/initialization; full status UI stays gated.
target_sources(recvx_android PRIVATE inventory_status.c "${decomp}/src/ps2/veronica/prog/sub1.c")
target_sources(recvx_android PRIVATE "${decomp}/src/ps2/veronica/prog/item.c")
target_sources(recvx_android PRIVATE inventory_sprite.c "${decomp}/src/ps2/veronica/prog/ps2_NaSprite.c")
target_compile_definitions(recvx_android PRIVATE RECVX_NATIVE_INVENTORY_RENDER)
set_source_files_properties("${decomp}/src/ps2/veronica/prog/ps2_NaSprite.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffp-contract=off;-fno-strict-aliasing")
set_source_files_properties("${decomp}/src/ps2/veronica/prog/sub1.c" PROPERTIES COMPILE_OPTIONS
    "-w;-Werror=implicit-function-declaration;-ffunction-sections;-fdata-sections;-ffp-contract=off;-fno-strict-aliasing;-DRECVX_NATIVE_INVENTORY_INIT_ONLY")
