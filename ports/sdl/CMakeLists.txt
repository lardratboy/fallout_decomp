cmake_minimum_required(VERSION 3.16)
project(fallout_port C)

# The game is compiled from src/ (never edited).  tools/emu/mkport.py derives
# the compiled copy: _BSS globals packed in original order, 16-bit arithmetic
# patches, accessors.  The test harness compiles the same generated source.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# --- SDL3 -------------------------------------------------------------------
# By default SDL3 is built from a pinned release and linked statically, so the
# game is self-contained.  -DFALLOUT_SYSTEM_SDL=ON links an installed SDL3
# instead (e.g. for distribution packages).  To build offline, point
# FETCHCONTENT_SOURCE_DIR_SDL3 at an unpacked SDL3 source tree.
option(FALLOUT_SYSTEM_SDL "Use an installed SDL3 instead of building it" OFF)
if(FALLOUT_SYSTEM_SDL)
    find_package(SDL3 REQUIRED CONFIG)
    set(FALLOUT_SDL SDL3::SDL3)
else()
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    # subsystems the game does not use
    foreach(sub GPU CAMERA JOYSTICK HAPTIC HIDAPI SENSOR DIALOG TRAY)
        set(SDL_${sub} OFF CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
        URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
        DOWNLOAD_EXTRACT_TIMESTAMP ON)
    FetchContent_MakeAvailable(SDL3)
    set(FALLOUT_SDL SDL3::SDL3-static)
endif()

set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
add_custom_command(
    OUTPUT "${GEN_DIR}/fallout_port.c" "${GEN_DIR}/bss_layout.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GEN_DIR}"
    COMMAND Python3::Interpreter "${REPO_ROOT}/tools/emu/mkport.py"
            "${REPO_ROOT}/src/fallout.c" "${GEN_DIR}"
    DEPENDS "${REPO_ROOT}/src/fallout.c"
            "${REPO_ROOT}/tools/emu/mkport.py"
            "${REPO_ROOT}/tools/emu/mkbss.py"
    COMMENT "Generating fallout_port.c from src/fallout.c"
    VERBATIM)

# --- the game itself (1991 code, compiled with DOS-like C semantics) --------
add_library(fallout_game OBJECT
    "${GEN_DIR}/fallout_port.c"
    src/gfx_port.c)
# shim/ must come before the system headers (dos.h, conio.h, io.h)
target_include_directories(fallout_game BEFORE PRIVATE
    shim src "${REPO_ROOT}/src" "${GEN_DIR}")
target_compile_definitions(fallout_game PRIVATE main=game_main)
if(MSVC)
    # MSVC: char is already signed, no type-based aliasing optimisation
    target_compile_options(fallout_game PRIVATE /w)
    set_source_files_properties("${GEN_DIR}/fallout_port.c" PROPERTIES
        COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/shim/fo_prefix.h")
else()
    # Turbo C: char is signed (it is not on Linux/aarch64), integers wrap,
    # and gfx.c reads bytes as 16-bit words
    target_compile_options(fallout_game PRIVATE
        -fsigned-char -fwrapv -fno-strict-aliasing -fno-common -w)
    set_source_files_properties("${GEN_DIR}/fallout_port.c" PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/shim/fo_prefix.h")
endif()

# --- the platform -----------------------------------------------------------
add_executable(fallout
    src/main.c
    src/machine.c
    src/tclib.c
    src/replay.c
    src/keyscript.c
    src/crc32.c
    src/rt.c
    src/video.c
    src/audio.c
    src/speaker.c
    src/keymap.c
    $<TARGET_OBJECTS:fallout_game>)
target_include_directories(fallout PRIVATE shim src)
target_link_libraries(fallout PRIVATE ${FALLOUT_SDL})

add_executable(port_unit tests/test_port.c src/keymap.c src/speaker.c)
target_include_directories(port_unit PRIVATE src)
target_link_libraries(port_unit PRIVATE ${FALLOUT_SDL})

foreach(t fallout port_unit)
    if(UNIX AND NOT APPLE)
        target_link_libraries(${t} PRIVATE m)
    endif()
    if(MSVC)
        target_compile_options(${t} PRIVATE /W3)
        target_compile_definitions(${t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -fno-common)
    endif()
endforeach()

# --- tests ------------------------------------------------------------------
enable_testing()
add_test(NAME unit COMMAND port_unit)
if(NOT WIN32)
    add_test(NAME parity
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity.sh" $<TARGET_FILE:fallout>)
endif()
