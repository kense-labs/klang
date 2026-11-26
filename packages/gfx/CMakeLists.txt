# Adapted for K. The gfx package's native part includes SDL3 and the C layers that gfx.k (over
# SDL_GPU), audio.k (SDL's audio) and ui.k (Nuklear, drawn through
# gfx) declare their extern fns against, under src/. One package, since the
# three share SDL and ui draws through gfx. Built by the K compiler
# through its wrapper project when a program needs the package
# (docs/design/packages.md §5.2), or into the compiler itself as a builtin
# package (§7.1). What a user of the package links is k_gfx_package,
# which carries all three layers and SDL.

cmake_minimum_required(VERSION 3.20)
project(k_gfx_package C)

# --- a video backend on Linux ------------------------------------------------
# SDL's configure does not fail without the X11 and Wayland development
# headers: it quietly builds an SDL that cannot open a window. Build nothing
# instead, saying what to install.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 QUIET)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GFX_WAYLAND QUIET wayland-client)
    endif()
    if(NOT X11_FOUND AND NOT GFX_WAYLAND_FOUND)
        message(STATUS "gfx: neither the X11 nor the Wayland development files were found, "
                       "so SDL could not open a window; building nothing. "
                       "Install the packages in SDL's docs/README-linux.md "
                       "(at least libx11-dev libxext-dev, or libwayland-dev libxkbcommon-dev "
                       "wayland-protocols) and reconfigure.")
        return()
    endif()
endif()

# Fetch exactly the dependency revisions used by this package. Load only the
# sources here so the SDL options below apply before configuring SDL.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(k_sdl
    URL https://codeload.github.com/libsdl-org/SDL/tar.gz/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c
    URL_HASH SHA256=d246967187a4f37cc850608dbaf5cff9d1df4d731259903d25539755cdfcc657
    SOURCE_SUBDIR klang-source-only)
FetchContent_Declare(k_nuklear
    URL https://codeload.github.com/Immediate-Mode-UI/Nuklear/tar.gz/9f7750296f176e506c2b24ff55bc24495e2db750
    URL_HASH SHA256=3760831cb8957fe0208f3af7b16b45d525480afb602dfd39671125077258ef25
    SOURCE_SUBDIR klang-source-only)
FetchContent_MakeAvailable(k_sdl k_nuklear)

# --- SDL ---------------------------------------------------------------------
# Static only. SDL_DEPS_SHARED stays on: on Linux SDL then links only the C
# library and loads X11, Wayland, audio and udev at runtime, so a program
# gains no hard GUI dependencies. SDL_GPU draws through neither the 2D
# renderer nor OpenGL, and nothing uses the camera, so those are left out.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${k_sdl_SOURCE_DIR}" SDL EXCLUDE_FROM_ALL)

# --- the gfx layer -----------------------------------------------------------
add_library(k_gfx STATIC
    src/gfx/gfx_api.h
    src/gfx/gfx_blob.h
    src/gfx/gfx_internal.h
    src/gfx/gfx_device.c
    src/gfx/gfx_resources.c
    src/gfx/gfx_pipeline.c
    src/gfx/gfx_draw.c
)
target_link_libraries(k_gfx PUBLIC SDL3::SDL3-static)

# --- the audio layer: the PCM mixer, no window ---------------------------------
add_library(k_audio STATIC src/audio/audio.c src/audio/audio_api.h)
target_link_libraries(k_audio PUBLIC SDL3::SDL3-static)

# --- the ui layer: Nuklear, one header, compiled once in ui_nuklear.c ----------
set(ui_layer_sources
    src/ui/ui_core.c
    src/ui/ui_font.c
    src/ui/ui_window.c
    src/ui/ui_widget.c
    src/ui/ui_style.c
    src/ui/ui_draw.c
)
add_library(k_ui STATIC src/ui/ui_api.h src/ui/ui_internal.h src/ui/ui_nuklear.c ${ui_layer_sources})
target_include_directories(k_ui PRIVATE "${k_nuklear_SOURCE_DIR}")
if(NOT WIN32)
    target_link_libraries(k_ui PUBLIC m)
endif()

foreach(t IN ITEMS k_gfx k_audio k_ui)
    set_target_properties(${t} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
                          INTERPROCEDURAL_OPTIMIZATION OFF)
endforeach()
# Warnings for the layers, none for Nuklear's implementation: third-party
# code is not ours to act on.
if(MSVC)
    foreach(t IN ITEMS k_gfx k_audio)
        target_compile_options(${t} PRIVATE /W4 /utf-8)
    endforeach()
    set_source_files_properties(${ui_layer_sources} PROPERTIES COMPILE_OPTIONS "/W4;/utf-8")
    set_source_files_properties(src/ui/ui_nuklear.c PROPERTIES COMPILE_OPTIONS "/W0;/utf-8")
    foreach(t IN ITEMS k_gfx k_audio k_ui)
        target_compile_definitions(${t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endforeach()
else()
    foreach(t IN ITEMS k_gfx k_audio)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    endforeach()
    set_source_files_properties(${ui_layer_sources} PROPERTIES COMPILE_OPTIONS
                                "-Wall;-Wextra;-Wno-missing-field-initializers")
    set_source_files_properties(src/ui/ui_nuklear.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

# --- the package -------------------------------------------------------------
add_library(k_gfx_package INTERFACE)
target_link_libraries(k_gfx_package INTERFACE k_gfx k_audio k_ui)

# The audio layer's own test, never part of a normal build: SDL's dummy
# driver, no device.
add_executable(k_audio_test EXCLUDE_FROM_ALL test/audio_device.c)
target_link_libraries(k_audio_test PRIVATE k_audio)
set_target_properties(k_audio_test PROPERTIES C_STANDARD 11)

# The window input test requires a display and GPU.
add_executable(k_gfx_input_test EXCLUDE_FROM_ALL test/window/gfx_input.c)
target_link_libraries(k_gfx_input_test PRIVATE k_gfx)
set_target_properties(k_gfx_input_test PROPERTIES C_STANDARD 11)

file(STRINGS "${k_sdl_SOURCE_DIR}/include/SDL3/SDL_version.h" gfx_sdl_version
     REGEX "^#define SDL_(MAJOR|MINOR|MICRO)_VERSION +[0-9]+")
string(REGEX REPLACE "[^;]* ([0-9]+);[^;]* ([0-9]+);[^;]* ([0-9]+)" "\\1.\\2.\\3"
       gfx_sdl_version "${gfx_sdl_version}")
file(READ "${k_nuklear_SOURCE_DIR}/clib.json" ui_clib)
string(REGEX MATCH "\"version\": *\"([0-9.]+)\"" ui_version_match "${ui_clib}")
message(STATUS "gfx: SDL ${gfx_sdl_version} and Nuklear ${CMAKE_MATCH_1}, static")
