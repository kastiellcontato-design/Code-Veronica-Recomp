cmake_minimum_required(VERSION 3.20)
project(MyGameRecomp
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Recompiled PS3 game project (ps3recomp)"
)

# ---------------------------------------------------------------------------
# Standards
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer Ninja if available
if(NOT CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED ENV{CMAKE_GENERATOR})
    message(STATUS "Tip: use -G Ninja for faster builds")
endif()

# ---------------------------------------------------------------------------
# The two directories this project is built from
# ---------------------------------------------------------------------------
# PS3RECOMP_DIR is the toolkit checkout: include/, runtime/, libs/, tools/.
# RECOMP_DIR is where the lifter's output for THIS game lives. They are
# separate on purpose -- a port keeps its generated code in its own tree and
# points at whatever toolkit checkout it is building against:
#
#   cmake -B build -G Ninja \
#         -DPS3RECOMP_DIR=/path/to/ps3recomp \
#         -DRECOMP_DIR=/path/to/my_game/recompiled
#
if(NOT DEFINED PS3RECOMP_DIR)
    # Default: this project is still sitting inside ps3recomp/templates/project/
    get_filename_component(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../" ABSOLUTE)
endif()
get_filename_component(PS3RECOMP_DIR "${PS3RECOMP_DIR}" ABSOLUTE)

set(RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/recompiled" CACHE PATH
    "Directory holding the lifter's output: ppu_recomp.h and the lifted sources")
get_filename_component(RECOMP_DIR "${RECOMP_DIR}" ABSOLUTE)

message(STATUS "ps3recomp SDK:   ${PS3RECOMP_DIR}")
message(STATUS "Lifted output:   ${RECOMP_DIR}")

if(NOT EXISTS "${RECOMP_DIR}/ppu_recomp.h")
    message(FATAL_ERROR
        "No ppu_recomp.h in ${RECOMP_DIR}.\n"
        "That header and the lifted sources beside it are what "
        "tools/ppu_lifter.py writes. Run the lifter, drop its output there, "
        "or point -DRECOMP_DIR= at wherever it already is.")
endif()

# The lifted translation units. A game's lift is hundreds of generated files,
# so they are globbed rather than listed; re-run CMake after a fresh lift.
file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS
    "${RECOMP_DIR}/*.c"
    "${RECOMP_DIR}/*.cpp"
)
if(NOT RECOMP_SOURCES)
    message(FATAL_ERROR
        "${RECOMP_DIR} has a ppu_recomp.h but no .c or .cpp beside it. The "
        "lifted functions and the function table live in those; without them "
        "there is nothing to run.")
endif()
list(LENGTH RECOMP_SOURCES RECOMP_SOURCE_COUNT)
message(STATUS "Lifted sources:  ${RECOMP_SOURCE_COUNT} file(s)")

# ---------------------------------------------------------------------------
# ps3recomp runtime library
# ---------------------------------------------------------------------------
# Built from source as a subdirectory rather than imported as a prebuilt
# archive. A static library carries none of its own link requirements, so an
# imported one arrives without SDL2, pthreads, the Metal frameworks or the
# Winsock/XInput imports -- the link then fails on every platform for a
# different reason. add_subdirectory hands all of that over with the target.
add_subdirectory("${PS3RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps3recomp_sdk"
                 EXCLUDE_FROM_ALL)

# The FMV audio tasks still execute through the SPU interpreter. Building its
# instruction loop with /Od cannot sustain a 5.33 ms audio-block deadline.
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND MSVC)
    set_property(SOURCE
        "${PS3RECOMP_DIR}/runtime/spu/spu_interp.c"
        "${PS3RECOMP_DIR}/runtime/spu/spu_channels.c"
        "${PS3RECOMP_DIR}/runtime/spu/spu_coherency.c"
        "${PS3RECOMP_DIR}/runtime/spu/spu_workload.c"
        TARGET_DIRECTORY ps3recomp_runtime
        APPEND PROPERTY COMPILE_OPTIONS "/O2" "/Ob2")
endif()

# SPU_ARCH (default AVX2): /arch for the lifted SPU code and the runtime's SPU
# files (interpreter, DMA, channels). Without it they built for bare SSE2 --
# shufb fell back to its byte loop and every fma was a call into ucrt's
# software fmaf -- and Code Veronica's audio mixer could not finish a 10.7 ms
# round in the 3D cutscenes. Contraction stays off: only the SPU's own fma
# instructions fuse. Empty = SSE2.
set(SPU_ARCH "AVX2" CACHE STRING "clang-cl /arch for SPU code (empty = SSE2)")
if(SPU_ARCH AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND MSVC)
    file(GLOB SPU_RUNTIME_SOURCES "${PS3RECOMP_DIR}/runtime/spu/*.c")
    set_property(SOURCE ${SPU_RUNTIME_SOURCES}
        TARGET_DIRECTORY ps3recomp_runtime
        APPEND PROPERTY COMPILE_OPTIONS "/arch:${SPU_ARCH}" "-ffp-contract=off")
endif()

# ---------------------------------------------------------------------------
# The HLE NID table
# ---------------------------------------------------------------------------
# Maps each firmware import NID onto the handler in the runtime that answers
# it. A port may keep a hand-curated one beside its lift; otherwise generate
# the canonical every-module table, which is what the toolkit's own boot
# harness uses.
if(EXISTS "${RECOMP_DIR}/ppu_hle_nids.cpp")
    set(HLE_NID_TABLE "${RECOMP_DIR}/ppu_hle_nids.cpp")
    message(STATUS "HLE NID table:   ${HLE_NID_TABLE}")
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(HLE_NID_TABLE "${CMAKE_CURRENT_BINARY_DIR}/ppu_hle_nids.cpp")
    add_custom_command(
        OUTPUT "${HLE_NID_TABLE}"
        COMMAND "${Python3_EXECUTABLE}"
                "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                --out "${HLE_NID_TABLE}" --all
        DEPENDS "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                "${PS3RECOMP_DIR}/tools/nid_database.py"
                "${PS3RECOMP_DIR}/libs/spurs/cellSpurs.c"
                "${PS3RECOMP_DIR}/libs/spurs/cellFiber.c"
                "${PS3RECOMP_DIR}/libs/codec/cellPamf.c"
                "${PS3RECOMP_DIR}/libs/codec/cellDmux.c"
                "${PS3RECOMP_DIR}/libs/codec/cellVdec.c"
                "${PS3RECOMP_DIR}/libs/codec/cellAdec.c"
        COMMENT "Generating the HLE NID -> handler table")
    message(STATUS "HLE NID table:   generated (every module)")
endif()

# ---------------------------------------------------------------------------
# The game executable
# ---------------------------------------------------------------------------
# The PPU boot scaffold is compiled here rather than taken from the runtime
# library: every one of these files #includes the lifter's per-game
# ppu_recomp.h, so they can only be built against a particular lift.
set(PS3RECOMP_SCAFFOLD
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_loader.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_hle.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_fs.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp"
)

# ---------------------------------------------------------------------------
# Lifted SPU images (optional)
# ---------------------------------------------------------------------------
# A title that runs SPU work gets its images lifted into <SPU_DIR>/<image>/ by
# tools/spu_lifter.py, with a registration TU generated by
# tools/build_spu_workloads.py. cellSpurs dispatches them by content
# fingerprint, so the generated table is keyed to the images THIS build ships:
# a table generated from another release registers signatures that can never
# match, which is worse than shipping none. Both the table and the job-module
# registry self-register from constructors and are therefore optional -- a tree
# that has not lifted its SPU images yet configures and links without them.
set(SPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spu" CACHE PATH
    "Directory holding the lifted SPU images (<SPU_DIR>/<image>/spu_recomp.c)")
option(PORT_ENABLE_SPU "Compile and register the title's lifted SPU images" ON)

set(SPU_SOURCES "")
if(PORT_ENABLE_SPU)
    file(GLOB SPU_LIFTED CONFIGURE_DEPENDS "${SPU_DIR}/*/spu_recomp.c")
    if(SPU_LIFTED)
        list(LENGTH SPU_LIFTED SPU_IMAGE_COUNT)
        message(STATUS "Lifted SPU:      ${SPU_IMAGE_COUNT} image(s)")
        # The musttail indirect-branch fast path. Excluded from the runtime
        # library on purpose (see the SDK root CMakeLists): it must be built
        # here, by clang, or the tail call degrades to a plain call and a guest
        # loop that branches indirectly walks the thread stack off its guard
        # page a few thousand iterations in.
        list(APPEND SPU_SOURCES ${SPU_LIFTED}
             "${PS3RECOMP_DIR}/runtime/spu/spu_dispatch_mt.c")
        # Generated beside the lift; each is present only once its generator
        # has been run, so each is included only if it is actually here.
        foreach(gen IN ITEMS spu_workloads.c jobmods_register.c taskset_policy_bytes.c)
            if(EXISTS "${SPU_DIR}/${gen}")
                list(APPEND SPU_SOURCES "${SPU_DIR}/${gen}")
            else()
                message(STATUS "  (no ${gen} -- not registered)")
            endif()
        endforeach()
    else()
        message(STATUS "Lifted SPU:      none found in ${SPU_DIR}")
    endif()
endif()

# Which EBOOT RECOMP_DIR holds: EU (NPEB00553, the default) or JP
# (NPJB00135). The port's own guest addresses follow it (cv_version.h); the
# JP build is MyGameRecomp_JP.exe, beside the EU one (build-release-jp.cmd).
set(CV_REGION "EU" CACHE STRING "Game version of the lift in RECOMP_DIR: EU, JP or US")
set_property(CACHE CV_REGION PROPERTY STRINGS EU JP US)
string(TOLOWER "${CV_REGION}" CV_REGION_LC)
option(CV_APPEARANCE "Separate character appearance build" OFF)
option(CV_CHARACTERS "Character appearance menu in normal play builds" ON)
if(CV_APPEARANCE AND NOT WIN32)
    message(FATAL_ERROR "Appearance rendering requires Windows")
endif()

# Guest OPTION menu hooks: create a build-local copy of only the affected
# lifted units. Direct and indirect guest calls keep their original names;
# the renamed bodies remain available to native_pc_menu.cpp as originals.
if(WIN32)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CV_MENU_HOOK_SCRIPT "${PS3RECOMP_DIR}/tools/cv_native_menu_hooks.py")
    if(CV_APPEARANCE OR CV_CHARACTERS)
        set(CV_MENU_HOOK_SCRIPT "${PS3RECOMP_DIR}/tools/cv_appearance_hooks.py")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CV_MENU_HOOK_SCRIPT}")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CV_MENU_HOOK_SCRIPT}" --discover "${RECOMP_DIR}"
                            --region "${CV_REGION_LC}"
        RESULT_VARIABLE CV_MENU_DISCOVERY_RESULT OUTPUT_VARIABLE CV_MENU_UNITS
        ERROR_VARIABLE CV_MENU_DISCOVERY_ERROR OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT CV_MENU_DISCOVERY_RESULT EQUAL 0)
        message(FATAL_ERROR "Cannot locate native menu hooks: ${CV_MENU_DISCOVERY_ERROR}")
    endif()
    foreach(CV_MENU_UNIT IN LISTS CV_MENU_UNITS)
        get_filename_component(CV_MENU_UNIT_NAME "${CV_MENU_UNIT}" NAME)
        set(CV_MENU_PATCHED "${CMAKE_CURRENT_BINARY_DIR}/native_menu_lift/${CV_MENU_UNIT_NAME}")
        add_custom_command(OUTPUT "${CV_MENU_PATCHED}"
            COMMAND "${Python3_EXECUTABLE}" "${CV_MENU_HOOK_SCRIPT}"
                    --input "${CV_MENU_UNIT}" --output "${CV_MENU_PATCHED}" --region "${CV_REGION_LC}"
            DEPENDS "${CV_MENU_UNIT}" "${CV_MENU_HOOK_SCRIPT}" VERBATIM)
        list(REMOVE_ITEM RECOMP_SOURCES "${CV_MENU_UNIT}")
        list(APPEND RECOMP_SOURCES "${CV_MENU_PATCHED}")
    endforeach()
endif()
add_executable(${PROJECT_NAME}
    main.cpp
    launcher.cpp
    cv_texture_export.cpp
    cv_srt.cpp
    cv_srt_data.cpp
    cv_cheats.cpp
    cv_srt_icons.cpp
    cv_srt_i18n.cpp
    button_icons.cpp
    patch_files.cpp
    log_viewer.cpp
    stubs.cpp
    ${PS3RECOMP_SCAFFOLD}
    ${HLE_NID_TABLE}
    ${RECOMP_SOURCES}
    ${SPU_SOURCES}
)

# The lifted SPU sources use bare #include "spu_context.h". Scope that include
# directory (and their flags) to those TUs alone: adding it to the whole target
# changes the compile command of every lifted PPU TU, and those are large enough
# that the needless rebuild is measured in tens of minutes.
#   SPU_USE_DISPATCH_MT -- route SPU_IB_DISPATCH through the musttail path.
#   -O2                 -- the target-wide -O1 exists for the huge PPU TUs; the
#                          SPU sources are small and decode-hot, so they get
#                          full optimization. The last -O on the line wins.
if(SPU_SOURCES)
    set_source_files_properties(${SPU_SOURCES} PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu"
        COMPILE_DEFINITIONS SPU_USE_DISPATCH_MT)
    if(NOT MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set_source_files_properties(${SPU_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2")
        if(SPU_ARCH)   # see SPU_ARCH above
            set_property(SOURCE ${SPU_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
                "/arch:${SPU_ARCH}" "-ffp-contract=off")
        endif()
    endif()
endif()

# Launcher art: every image in launcher_art/ becomes an RCDATA resource named
# after the file (banner.jpg -> BANNER), and icon.ico becomes the program icon
# (resource 1: Explorer, taskbar, both windows). The cover and the icon are the
# title's own artwork and are not in the repository; without them the launcher
# draws a plain banner and the exe keeps the default icon.
if(WIN32)
    set(CV_MENU_SOURCE native_pc_menu.cpp)
    if(CV_APPEARANCE OR CV_CHARACTERS)
        set(CV_MENU_SOURCE cv_appearance_menu.cpp)
        target_sources(${PROJECT_NAME} PRIVATE cv_appearance_assets.cpp cv_appearance_mesh.cpp
            cv_appearance_preview.cpp cv_appearance_menu_preview.cpp cv_appearance_runtime.cpp)
        set_source_files_properties(cv_appearance_assets.cpp PROPERTIES INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/codec")
    endif()
    target_sources(${PROJECT_NAME} PRIVATE ${CV_MENU_SOURCE} boot_skip.cpp cv_test_tools.cpp cv_fps_experimental.cpp cv_native_libc.cpp cv_focus_pause.cpp cv_items.cpp cv_item_menu.cpp)
    file(GLOB LAUNCHER_ART CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/launcher_art/*.png"
         "${CMAKE_CURRENT_SOURCE_DIR}/launcher_art/*.jpg")
    set(_launcher_rc "")
    foreach(_f IN LISTS LAUNCHER_ART)
        get_filename_component(_n "${_f}" NAME_WE)
        string(TOUPPER "${_n}" _n)
        string(APPEND _launcher_rc "${_n} RCDATA \"${_f}\"
")
    endforeach()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/launcher_art/icon.ico")
        string(APPEND _launcher_rc "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/launcher_art/icon.ico\"
")
    endif()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/launcher_art.rc" "${_launcher_rc}")
    # An image redrawn under the same name rebuilds the resources too.
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/launcher_art.rc" PROPERTIES OBJECT_DEPENDS "${LAUNCHER_ART}")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/launcher_art.rc")
    set_source_files_properties(launcher.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/input")
endif()

# app.rc carries the icon, the version block and the manifest, and needs a
# resource compiler. Listing it as a source on a host that has none is not a
# link error, it is a "no rule to make" for a file nothing can consume.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    "${RECOMP_DIR}"                            # the generated ppu_recomp.h
    "${PS3RECOMP_DIR}/include"
    "${PS3RECOMP_DIR}/runtime/ppu"
    "${PS3RECOMP_DIR}/runtime/spu"
    "${PS3RECOMP_DIR}/runtime/memory"
    "${PS3RECOMP_DIR}/runtime/syscalls"
    "${PS3RECOMP_DIR}/runtime/platform"        # win32_compat.h and friends
    "${PS3RECOMP_DIR}/libs/video"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ps3recomp_runtime
)

# The runtime is C++, and on Apple also Objective-C. Letting CMake infer the
# link language from the generated C would leave the C++ standard library out.
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# The Japanese build: its own guest addresses (cv_version.h) and its own name,
# so both executables share one folder (DLLs, launcher settings, caches).
if(CV_REGION_LC STREQUAL "jp")
    target_compile_definitions(${PROJECT_NAME} PRIVATE CV_REGION_JP=1)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}_JP")
elseif(CV_REGION_LC STREQUAL "us")
    target_compile_definitions(${PROJECT_NAME} PRIVATE CV_REGION_US=1)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}_US")
elseif(NOT CV_REGION_LC STREQUAL "eu")
    message(FATAL_ERROR "CV_REGION must be EU, JP or US, not ${CV_REGION}")
endif()

# Windows system import libraries. dbghelp symbolizes crash-handler addresses
# against the PDB, user32 and gdi32 are what the backend's window and its GDI
# present are made of, and winmm is timeBeginPeriod. None of the four names
# anything a POSIX linker can find; the D3D12 backend's own imports come in
# through the #pragma comment(lib) in libs/video/rsx_d3d12_backend.c.
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dbghelp
        user32
        gdi32
        winmm
        dxguid  # IID_IDXGIFactory6 (D3D12 adapter pick); the optimized link needs it
    )
endif()

# ---------------------------------------------------------------------------
# Compiler-specific flags
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fno-strict-aliasing
    )
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3 /wd4100 /wd4244 /wd4267
        /Zc:__cplusplus         # report correct __cplusplus value
        # ppu_recomp.h defines PPU_FENCE, the lowering of the guest's
        # sync/lwsync/eieio/isync, and in C that is <stdatomic.h>. The lifter
        # writes C++ by default, but --single-file writes ppu_recomp.c, and a
        # port that took it would not compile without this. The runtime's own
        # build already carries the same flag for the same reason.
        /experimental:c11atomics
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# The lifted translation units are machine-generated and can run to hundreds of
# thousands of lines each, so they get their own flags:
#
#   - warnings off. They are about generated code and nobody will act on them,
#     and at this volume they bury the diagnostics that matter.
#   - no floating-point contraction. arm64 fuses a multiply and an add by
#     default; a PPC fmadd is spelt as one where the lifter means one.
#   - /bigobj on MSVC. A single lifted file routinely passes the 65,536-section
#     limit of the default object format.
if(MSVC)
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/w;/bigobj")
else()
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-w;-ffp-contract=off")
endif()

# Lifted loads and stores take the inline fast path in the generated
# preamble (see "Inline guest memory access" there) instead of a call per
# access. -DPPU_VM_INLINE=OFF builds the old call-per-access code.
option(PPU_VM_INLINE "Inline guest memory access in lifted code" ON)
if(PPU_VM_INLINE)
    set_property(SOURCE ${RECOMP_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS PPU_VM_INLINE=1)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

# FFmpeg 7.1 headers; matching shared libraries are packaged beside the executable.
target_sources(ps3recomp_runtime PRIVATE "${PS3RECOMP_DIR}/libs/codec/cv_ffmpeg.c")
target_include_directories(ps3recomp_runtime PRIVATE "${PS3RECOMP_DIR}/third_party/ffmpeg/include")
# Debug info for the runtime only (not the lifted game code, which would make
# the link enormous): profiler samples then resolve to exact functions,
# static ones included, with llvm-symbolizer. The /OPT flags keep the release
# link identical to one without /DEBUG.
if(MSVC)
    target_compile_options(ps3recomp_runtime PRIVATE /Z7)
    target_link_options(${PROJECT_NAME} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
endif()

foreach(FFMPEG_DLL avcodec-61.dll avutil-59.dll swscale-8.dll swresample-5.dll)
    add_custom_command(TARGET MyGameRecomp POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PS3RECOMP_DIR}/third_party/ffmpeg/bin/${FFMPEG_DLL}"
        "$<TARGET_FILE_DIR:MyGameRecomp>/${FFMPEG_DLL}")
endforeach()

# Opt-in regression check, built only through build-release.cmd when
# CV_FP_TESTS=1. It has no game assets, runtime or graphics-device dependency.
add_executable(code_veronica_fp_tests EXCLUDE_FROM_ALL
    "${PS3RECOMP_DIR}/libs/video/tests/test_fp_decompiler.c"
    "${PS3RECOMP_DIR}/libs/video/rsx_fp_decompiler.c")
target_include_directories(code_veronica_fp_tests PRIVATE "${PS3RECOMP_DIR}/include")
if(WIN32)
    add_executable(code_veronica_spu_fault_tests EXCLUDE_FROM_ALL
        "${PS3RECOMP_DIR}/runtime/spu/tests/test_spu_fault_snapshot.c"
        "${PS3RECOMP_DIR}/runtime/spu/spu_fault_snapshot.c")
    target_compile_options(code_veronica_spu_fault_tests PRIVATE /UNDEBUG)
    add_executable(code_veronica_spu_pool_tests EXCLUDE_FROM_ALL
        "${PS3RECOMP_DIR}/runtime/spu/tests/test_spu_task_pool.c")
    target_compile_options(code_veronica_spu_pool_tests PRIVATE /UNDEBUG)
    add_executable(code_veronica_spu_schedule_tests EXCLUDE_FROM_ALL
        "${PS3RECOMP_DIR}/runtime/spu/tests/test_spu_taskset_schedule.c"
        "${PS3RECOMP_DIR}/runtime/spu/spu_taskset_schedule.c")
    target_compile_options(code_veronica_spu_schedule_tests PRIVATE /Gy /UNDEBUG)
    target_link_options(code_veronica_spu_schedule_tests PRIVATE /OPT:REF)
    add_executable(code_veronica_dxc_tests EXCLUDE_FROM_ALL
        "${PS3RECOMP_DIR}/libs/video/tests/test_dxc.cpp"
        "${PS3RECOMP_DIR}/libs/video/rsx_dxc.cpp")
    target_link_libraries(code_veronica_dxc_tests PRIVATE d3d12 d3dcompiler dxgi)
endif()

if(CV_APPEARANCE)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "MyGameRecomp_${CV_REGION}_Characters")
endif()

# Exercises real callback-driven reads/writes in isolated temporary folders.
add_executable(code_veronica_save_tests EXCLUDE_FROM_ALL
    "${PS3RECOMP_DIR}/libs/system/tests/test_savedata_io.c")
target_include_directories(code_veronica_save_tests PRIVATE "${PS3RECOMP_DIR}/include")
if(MSVC)
    target_compile_options(code_veronica_save_tests PRIVATE /Gy /UNDEBUG)
    target_link_options(code_veronica_save_tests PRIVATE /OPT:REF)
else()
    target_compile_options(code_veronica_save_tests PRIVATE -UNDEBUG)
endif()

if(WIN32)
    add_executable(code_veronica_menu_tests EXCLUDE_FROM_ALL tests/test_pc_menu.cpp)
    set_target_properties(code_veronica_menu_tests PROPERTIES OUTPUT_NAME "code_veronica_menu_tests_${CV_REGION_LC}")
    target_include_directories(code_veronica_menu_tests PRIVATE "${RECOMP_DIR}")
    target_compile_options(code_veronica_menu_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_menu_tests PRIVATE gdi32 user32)
    if(CV_REGION_LC STREQUAL "jp")
        target_compile_definitions(code_veronica_menu_tests PRIVATE CV_REGION_JP=1)
    elseif(CV_REGION_LC STREQUAL "us")
        target_compile_definitions(code_veronica_menu_tests PRIVATE CV_REGION_US=1)
    endif()
endif()

if(WIN32)
    add_executable(code_veronica_srt_tests EXCLUDE_FROM_ALL
        tests/test_srt.cpp cv_srt.cpp cv_srt_data.cpp cv_srt_icons.cpp cv_srt_i18n.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/launcher_art.rc"
        "${PS3RECOMP_DIR}/libs/filesystem/edat.c")
    target_compile_options(code_veronica_srt_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_srt_tests PRIVATE gdi32 user32 gdiplus ole32)
    set_target_properties(code_veronica_srt_tests PROPERTIES OUTPUT_NAME "cv_srt_tests_${CV_REGION_LC}")
    if(CV_REGION_LC STREQUAL "jp")
        target_compile_definitions(code_veronica_srt_tests PRIVATE CV_REGION_JP=1)
    elseif(CV_REGION_LC STREQUAL "us")
        target_compile_definitions(code_veronica_srt_tests PRIVATE CV_REGION_US=1)
    endif()
endif()

# Optional tests of host map commands against isolated synthetic guest memory.
if(WIN32)
    add_executable(code_veronica_map_tests EXCLUDE_FROM_ALL tests/test_map_tools.cpp cv_items.cpp)
    target_include_directories(code_veronica_map_tests PRIVATE "${RECOMP_DIR}")
    target_compile_options(code_veronica_map_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_map_tests PRIVATE user32 gdi32)
    if(CV_REGION_LC STREQUAL "jp")
        target_compile_definitions(code_veronica_map_tests PRIVATE CV_REGION_JP=1)
        set_target_properties(code_veronica_map_tests PROPERTIES OUTPUT_NAME "code_veronica_map_tests_jp")
    elseif(CV_REGION_LC STREQUAL "us")
        target_compile_definitions(code_veronica_map_tests PRIVATE CV_REGION_US=1)
        set_target_properties(code_veronica_map_tests PROPERTIES OUTPUT_NAME "code_veronica_map_tests_us")
    endif()
endif()

if(WIN32)
    add_executable(code_veronica_fps_tests EXCLUDE_FROM_ALL tests/test_fps_experimental.cpp cv_fps_experimental.cpp)
    target_include_directories(code_veronica_fps_tests PRIVATE "${RECOMP_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(code_veronica_fps_tests PRIVATE /UNDEBUG)
    if(CV_REGION_LC STREQUAL "jp")
        target_compile_definitions(code_veronica_fps_tests PRIVATE CV_REGION_JP=1)
    elseif(CV_REGION_LC STREQUAL "us")
        target_compile_definitions(code_veronica_fps_tests PRIVATE CV_REGION_US=1)
    endif()
endif()

# CPU-only checks for render interpolation. No game assets or GPU required.
add_executable(code_veronica_pose_replay_tests EXCLUDE_FROM_ALL tests/test_pose_capture_replay.cpp)
target_include_directories(code_veronica_pose_replay_tests PRIVATE "${PS3RECOMP_DIR}/libs/video")
if(MSVC)
    target_compile_options(code_veronica_pose_replay_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(code_veronica_pose_replay_tests PRIVATE -UNDEBUG)
endif()
add_executable(code_veronica_pose_tests EXCLUDE_FROM_ALL tests/test_pose_interpolation.cpp)
target_include_directories(code_veronica_pose_tests PRIVATE "${PS3RECOMP_DIR}/libs/video")
if(MSVC)
    target_compile_options(code_veronica_pose_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(code_veronica_pose_tests PRIVATE -UNDEBUG)
endif()

# GPU regression for packed UNORM16 filtering at byte carry boundaries.
if(WIN32)
    add_executable(code_veronica_pose_gpu_tests EXCLUDE_FROM_ALL tests/test_pose_gpu.cpp
        "${PS3RECOMP_DIR}/libs/video/rsx_vertex_fetch.c")
    target_include_directories(code_veronica_pose_gpu_tests PRIVATE "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/include")
    target_compile_options(code_veronica_pose_gpu_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_pose_gpu_tests PRIVATE d3d11 d3dcompiler)
    add_executable(code_veronica_pose_blend_tests EXCLUDE_FROM_ALL tests/test_pose_blend_d3d12.cpp
        "${PS3RECOMP_DIR}/libs/video/rsx_dxc.cpp")
    target_include_directories(code_veronica_pose_blend_tests PRIVATE "${PS3RECOMP_DIR}/libs/video")
    target_compile_options(code_veronica_pose_blend_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_pose_blend_tests PRIVATE d3d12 dxgi d3dcompiler ole32)
    add_executable(code_veronica_shadow_filter_tests EXCLUDE_FROM_ALL
        "${PS3RECOMP_DIR}/libs/video/tests/test_packed16_filter.cpp")
    target_include_directories(code_veronica_shadow_filter_tests PRIVATE "${PS3RECOMP_DIR}/libs/video")
    target_link_libraries(code_veronica_shadow_filter_tests PRIVATE d3d11 d3dcompiler)
endif()

# The F6 item menu: catalog, placement, the guest write and the drawing,
# against synthetic memory with this build's region addresses.
if(WIN32)
    add_executable(code_veronica_items_tests EXCLUDE_FROM_ALL
        tests/test_items.cpp cv_items.cpp cv_item_menu.cpp cv_srt_icons.cpp cv_srt_i18n.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/launcher_art.rc"
        "${PS3RECOMP_DIR}/libs/filesystem/edat.c")
    target_compile_options(code_veronica_items_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_items_tests PRIVATE gdi32 user32 gdiplus ole32 dwmapi)
    set_target_properties(code_veronica_items_tests PROPERTIES OUTPUT_NAME "cv_items_tests_${CV_REGION_LC}")
    if(CV_REGION_LC STREQUAL "jp")
        target_compile_definitions(code_veronica_items_tests PRIVATE CV_REGION_JP=1)
    elseif(CV_REGION_LC STREQUAL "us")
        target_compile_definitions(code_veronica_items_tests PRIVATE CV_REGION_US=1)
    endif()
endif()

# Isolated cheat state/store tests against each region's own addresses.
if(WIN32)
    foreach(cheat_region IN ITEMS eu jp us)
        add_executable(cv_cheats_tests_${cheat_region} EXCLUDE_FROM_ALL tests/test_cheats.cpp cv_cheats.cpp)
        target_compile_options(cv_cheats_tests_${cheat_region} PRIVATE /UNDEBUG)
        if(cheat_region STREQUAL "jp")
            target_compile_definitions(cv_cheats_tests_${cheat_region} PRIVATE CV_REGION_JP=1)
        elseif(cheat_region STREQUAL "us")
            target_compile_definitions(cv_cheats_tests_${cheat_region} PRIVATE CV_REGION_US=1)
        endif()
    endforeach()
    add_custom_target(code_veronica_cheat_tests DEPENDS cv_cheats_tests_eu cv_cheats_tests_jp cv_cheats_tests_us)
endif()

# Pause on focus loss: the decision (which windows count as the game's focus,
# entering and leaving the pause, the option off). No window or game needed.
add_executable(code_veronica_focus_pause_tests EXCLUDE_FROM_ALL tests/test_focus_pause.cpp)
target_include_directories(code_veronica_focus_pause_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(code_veronica_focus_pause_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(code_veronica_focus_pause_tests PRIVATE -UNDEBUG)
endif()

# Visual contact sheet using the exact keyboard renderer shipped by the game.
if(WIN32)
    add_executable(code_veronica_keycap_preview EXCLUDE_FROM_ALL tests/preview_keyboard_icons.cpp)
    target_link_libraries(code_veronica_keycap_preview PRIVATE gdiplus)
endif()

if(WIN32)
    add_executable(code_veronica_hd_button_tests EXCLUDE_FROM_ALL tests/test_hd_buttons.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/launcher_art.rc"
        "${PS3RECOMP_DIR}/libs/filesystem/edat.c")
    target_compile_options(code_veronica_hd_button_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_hd_button_tests PRIVATE gdiplus ole32 user32 gdi32)
endif()

# Sampled frame timing: production query probe on D3D12 WARP, without game data.
if(WIN32)
    add_executable(code_veronica_frame_probe_tests EXCLUDE_FROM_ALL tests/test_frame_gpu_probe.cpp)
    target_include_directories(code_veronica_frame_probe_tests PRIVATE "${PS3RECOMP_DIR}/libs/video")
    target_compile_options(code_veronica_frame_probe_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_frame_probe_tests PRIVATE d3d12 dxgi dxguid)
endif()

# Offline archive extraction tests: no renderer, game process or inputs.
if(WIN32)
    add_executable(code_veronica_texture_export_tests EXCLUDE_FROM_ALL
        tests/test_texture_export.cpp tests/texture_export_stb.c cv_texture_export.cpp
        "${PS3RECOMP_DIR}/libs/filesystem/edat.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_texture_pack.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_texture_layout.c")
    target_include_directories(code_veronica_texture_export_tests PRIVATE "${PS3RECOMP_DIR}/include")
    target_compile_options(code_veronica_texture_export_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_texture_export_tests PRIVATE gdiplus ole32)
endif()

# Bounded FIFO event wake with real Windows synchronization, no game assets.
if(WIN32)
    add_executable(code_veronica_fifo_idle_tests EXCLUDE_FROM_ALL tests/test_fifo_idle.cpp)
    target_compile_options(code_veronica_fifo_idle_tests PRIVATE /UNDEBUG)
    target_include_directories(code_veronica_fifo_idle_tests PRIVATE "${PS3RECOMP_DIR}/runtime/platform")
endif()

# Production coherence registry regression, host wake edges stubbed only.
if(WIN32)
    add_executable(code_veronica_spu_coherence_tests EXCLUDE_FROM_ALL tests/test_spu_coherency_registry.c)
    target_compile_options(code_veronica_spu_coherence_tests PRIVATE /UNDEBUG)
endif()

# GPU compute interpolation: production six-pass shaders vs CPU on WARP and hardware.
if(WIN32)
    add_executable(code_veronica_pose_compute_tests EXCLUDE_FROM_ALL tests/test_pose_compute.cpp
        "${PS3RECOMP_DIR}/libs/video/rsx_dxc.cpp")
    target_include_directories(code_veronica_pose_compute_tests PRIVATE "${PS3RECOMP_DIR}/libs/video")
    target_compile_options(code_veronica_pose_compute_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_pose_compute_tests PRIVATE d3d12 dxgi dxguid d3dcompiler)
endif()

# Optional official analytical FSR 3.1.6. Native/disabled mode never loads it.
if(WIN32)
    set(CV_FSR31_DIR "${PS3RECOMP_DIR}/third_party/amd_fsr31")
    if(EXISTS "${CV_FSR31_DIR}/bin/amd_fidelityfx_framegeneration_dx12.dll")
        add_custom_command(TARGET MyGameRecomp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CV_FSR31_DIR}/bin/amd_fidelityfx_framegeneration_dx12.dll"
                "$<TARGET_FILE_DIR:MyGameRecomp>/amd_fidelityfx_framegeneration_dx12.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CV_FSR31_DIR}/LICENSE.md"
                "$<TARGET_FILE_DIR:MyGameRecomp>/AMD-FSR-LICENSE.md")
    endif()
    add_executable(code_veronica_motion_capture_tests EXCLUDE_FROM_ALL tests/test_motion_capture.c)
    target_include_directories(code_veronica_motion_capture_tests PRIVATE "${PS3RECOMP_DIR}/libs/video")
    target_compile_options(code_veronica_motion_capture_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_motion_capture_tests PRIVATE d3d12 dxgi dxguid user32)
    add_executable(code_veronica_fsr31_tests EXCLUDE_FROM_ALL tests/test_fsr31.cpp
        "${PS3RECOMP_DIR}/libs/video/rsx_fsr31.cpp" "${PS3RECOMP_DIR}/libs/video/rsx_dxc.cpp")
    target_include_directories(code_veronica_fsr31_tests PRIVATE "${PS3RECOMP_DIR}/libs/video")
    target_compile_options(code_veronica_fsr31_tests PRIVATE /UNDEBUG)
    target_link_libraries(code_veronica_fsr31_tests PRIVATE d3d12 dxgi dxguid d3dcompiler user32)
    if(EXISTS "${CV_FSR31_DIR}/bin/amd_fidelityfx_framegeneration_dx12.dll")
        add_custom_command(TARGET code_veronica_fsr31_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CV_FSR31_DIR}/bin/amd_fidelityfx_framegeneration_dx12.dll"
                "$<TARGET_FILE_DIR:code_veronica_fsr31_tests>/amd_fidelityfx_framegeneration_dx12.dll")
    endif()
endif()
