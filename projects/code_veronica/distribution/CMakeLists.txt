cmake_minimum_required(VERSION 3.20)
project(CodeVeronicaDistribution LANGUAGES C CXX)
get_filename_component(PORT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
get_filename_component(SDK "${PORT}/../.." ABSOLUTE)
set(CV_REGION "EU" CACHE STRING "Distribution region")
string(TOLOWER "${CV_REGION}" REGION)
set(PS3RECOMP_DIR "${SDK}" CACHE PATH "SDK" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PORT}/build-distribution/bin")
foreach(flag CV_TRACY CV_WORK_PROFILE CV_MESH_ID_DISCOVERY CV_SPU_NATIVE_DRAW CV_SPU_NATIVE_DESCRIPTORS CV_APPEARANCE PS3RECOMP_BUILD_TESTS)
    set(${flag} OFF CACHE BOOL "No developer builds in distribution" FORCE)
endforeach()
add_subdirectory("${PORT}" port)
target_compile_definitions(MyGameRecomp PRIVATE CV_DISTRIBUTION=1)
target_compile_definitions(ps3recomp_runtime PRIVATE CV_DISTRIBUTION=1)
target_sources(MyGameRecomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/windows_entry.cpp")
set_target_properties(MyGameRecomp PROPERTIES WIN32_EXECUTABLE TRUE)
# Optional reuse of audited unchanged lifted objects. All host/runtime/UI code
# is compiled for distribution. The preparation script verifies the source and
# ABI headers before copying the existing objects into this private cache.
if(EXISTS "${CMAKE_BINARY_DIR}/reuse.cmake")
    include("${CMAKE_BINARY_DIR}/reuse.cmake")
    foreach(source IN LISTS CV_LIFTED_OBJECTS)
        if(source MATCHES "\\.cpp$")
            set_property(SOURCE "${source}" TARGET_DIRECTORY MyGameRecomp
                PROPERTY COMPILE_OPTIONS "-w;-ffp-contract=off")
            if(PPU_VM_INLINE)
                set_property(SOURCE "${source}" TARGET_DIRECTORY MyGameRecomp
                    APPEND PROPERTY COMPILE_DEFINITIONS PPU_VM_INLINE=1)
            endif()
        endif()
    endforeach()
    get_target_property(sources MyGameRecomp SOURCES)
    set(filtered)
    foreach(source IN LISTS sources)
        if(source MATCHES "native_menu_lift/ppu_recomp_[0-9]+\\.cpp$" OR source MATCHES "/spu/[^/]+/spu_recomp\\.c$")
            continue()
        endif()
        list(APPEND filtered "${source}")
    endforeach()
    set_property(TARGET MyGameRecomp PROPERTY SOURCES "${filtered};${CV_LIFTED_OBJECTS}")
endif()

# One public entry point; regional runtimes are packaged under bin/.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/launcher_entry.rc"
    "1 ICON \"${PORT}/launcher_art/icon.ico\"\n")
add_executable(CodeVeronicaLauncher WIN32 launcher_entry.cpp "${CMAKE_CURRENT_BINARY_DIR}/launcher_entry.rc")
set_target_properties(CodeVeronicaLauncher PROPERTIES OUTPUT_NAME "Code Veronica"
    CXX_STANDARD 17 MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${PORT}/build-distribution")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/launcher_entry.rc" PROPERTIES
    OBJECT_DEPENDS "${PORT}/launcher_art/icon.ico")
