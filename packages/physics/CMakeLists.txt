# Adapted for K. The native part includes Box3D and k_physics, the C layer
# over it that physics.k declares its extern fns against (src/). Built by
# the K compiler through its wrapper project when a program needs the
# package (docs/design/packages.md §5.2), or into the compiler itself as a
# builtin package (§7.1).

cmake_minimum_required(VERSION 3.22)
project(k_physics_package C)

# Fetch the pinned Box3D source before applying the library's build options.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(k_box3d
    URL https://codeload.github.com/erincatto/box3d/tar.gz/f555ee42084e0b43cbffa863f40bff8117c08896
    URL_HASH SHA256=75c99b70170172b0dce7a1a6e3d83c29ecabcf3a4ba437b5bb72ee6429529e20
    SOURCE_SUBDIR klang-source-only)
FetchContent_MakeAvailable(k_box3d)
set_property(DIRECTORY PROPERTY K_PACKAGE_LICENSES "box3d\t${k_box3d_SOURCE_DIR}/LICENSE")

# --- Box3D -------------------------------------------------------------------
# Only the library: its samples, tests and documentation are options of a
# top-level build alone. Single precision, which the layer's structs are, and
# static whatever BUILD_SHARED_LIBS says elsewhere.
set(BOX3D_DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory("${k_box3d_SOURCE_DIR}" box3d EXCLUDE_FROM_ALL)
# Box3D asks for the debug C runtime in a debug build; everything of K's
# links the release one.
set_target_properties(box3d PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")

# --- the physics layer ---------------------------------------------------------
add_library(k_physics STATIC
    src/physics_api.h
    src/physics_internal.h
    src/physics_core.c
    src/physics_world.c
    src/physics_body.c
    src/physics_shape.c
    src/physics_joint.c
    src/physics_geometry.c
)
# Box3D starts worker threads of its own inside a step.
find_package(Threads REQUIRED)
target_link_libraries(k_physics PUBLIC box3d::box3d Threads::Threads)
# C17, as Box3D's headers are.
set_target_properties(k_physics PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON
                      INTERPROCEDURAL_OPTIMIZATION OFF)
if(MSVC)
    target_compile_options(k_physics PRIVATE /W4 /utf-8)
    target_compile_definitions(k_physics PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(k_physics PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

get_directory_property(physics_box3d_version DIRECTORY "${k_box3d_SOURCE_DIR}" DEFINITION box3d_VERSION)
message(STATUS "physics: Box3D ${physics_box3d_version}, static")
