cmake_minimum_required(VERSION 3.27)
project(klang C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# One C runtime for the compiler, TinyCC, SDL and the gfx library, and the
# one `cl game.c` links by default, so a program can link the same archives.
# The non-debug variant even in Debug builds: a /MTd archive does not link
# cleanly into a plain /MT program.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_executable(k
    src/main.cpp
    src/process.cpp
    src/version.cpp
    src/process.h
    src/progress.cpp
    src/progress.h
    src/buildfile.h
    src/toolchain.h
    src/build.h
    src/packages.h
    src/json.h
    src/native.h
    src/lsp_rpc.h
    src/lsp_analysis.h
    src/lsp_workspace.h
    src/lsp_features.h
    src/lsp.h
    src/format.h
    src/builtin.h
    src/includes.h
    src/utils.h
    src/small_vector.h
    src/flat_map.h
    src/lexer.h
    src/ast.h
    src/dump.h
    src/clone.h
    src/parser.h
    src/resolve.h
    src/expansion_bind.h
    src/typecheck_expansion.h
    src/builtins.h
    src/typecheck.h
    src/type_queries.h
    src/typecheck_types.h
    src/typecheck_exprs.h
    src/typecheck_flow.h
    src/typecheck_calls.h
    src/typecheck_builtins.h
    src/typecheck_nodes.h
    src/optimize.h
    src/optimize_basecase.h
    src/optimize_tre.h
    src/optimize_loops.h
    src/optimize_reassoc.h
    src/bce.h
    src/codegen.h
    src/codegen_types.h
    src/codegen_frames.h
    src/codegen_values.h
    src/codegen_construct.h
    src/codegen_stmts.h
    src/codegen_calls.h
    src/codegen_builtins.h
    src/codegen_render.h
    src/codegen_emit.h
    src/codegen_stacks.h
    src/codegen_merge.h
    src/codegen_nodes.h
    src/runtime_inline.h
    src/jit.h
    src/runtime/runtime.h
    src/runtime/runtime_impl.h
    src/runtime/runtime_threads.h
    src/runtime/runtime_ext.h
    src/runtime/runtime_os.h
    src/runtime/klang.h
    src/gfx.h
)

# Keep argv, CRT file access, filesystem paths and Win32's narrow APIs in
# the same encoding as K strings. Windows 10 1903+ applies this before
# the C runtime constructs argv; setlocale() in main would be too late.
if(MSVC)
    target_sources(k PRIVATE src/klang.manifest)
elseif(WIN32)
    enable_language(RC)
    target_sources(k PRIVATE src/klang.rc)
    set_property(SOURCE src/klang.rc APPEND PROPERTY OBJECT_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/src/klang.manifest")
endif()

# The compiler's version (src/version.cpp): K_VERSION where given, as a
# release gives its tag, else `git describe` of this tree at each build.
set(K_VERSION "" CACHE STRING "The compiler's version; empty for git describe")
set(k_version_h "${CMAKE_CURRENT_BINARY_DIR}/version/k_version.h")
add_custom_target(k_version
    COMMAND "${CMAKE_COMMAND}" "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUT=${k_version_h}"
            "-DVERSION=${K_VERSION}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake"
    BYPRODUCTS "${k_version_h}")
add_dependencies(k k_version)
target_include_directories(k PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version")

# The compile runs on a thread of its own, with a larger stack (main.cpp).
find_package(Threads REQUIRED)
target_link_libraries(k PRIVATE Threads::Threads)
# The library the process runner (process.cpp) is built on.
target_include_directories(k PRIVATE third_party/subprocess)

# A program run in-process runs on the main thread (main.cpp), whose stack the
# executable sets: 64 MB reserved, as for the compile thread, where Windows
# would give it 1 MB and macOS 8 MB. Only what a program's depth reaches is
# committed. On Windows this is also the reserve of every thread started
# without a size of its own (SDL's, a GPU driver's). A Linux main thread's
# stack comes from the RLIMIT_STACK the process starts with (`ulimit -s`,
# commonly 8 MB), which no link option sets.
set(k_main_stack 67108864)
if(MSVC)
    target_link_options(k PRIVATE "/STACK:${k_main_stack}")
elseif(MINGW)
    # Statically link the runtime to avoid shipping libgcc, libstdc++, and libwinpthread DLLs.
    target_link_options(k PRIVATE "LINKER:--stack,${k_main_stack}" -static -static-libgcc -static-libstdc++)
elseif(APPLE)
    math(EXPR k_main_stack_hex "${k_main_stack}" OUTPUT_FORMAT HEXADECIMAL)
    target_link_options(k PRIVATE "LINKER:-stack_size,${k_main_stack_hex}")
endif()

# The shader compiler behind embed_shader: third-party C (third_party/
# cute_spirv), always built, so a gfx program typechecks and emits the same C
# with or without SDL.
add_library(k_shaderc STATIC src/shaderc.c src/shaderc.h src/gfx_blob.h)
target_include_directories(k_shaderc PRIVATE third_party/cute_spirv)
if(MSVC)
    # ckit.h uses typeof and C11 atomics, which MSVC has only in its latest C
    # mode. /W0 as for TinyCC: third-party warnings are not ours to act on.
    target_compile_options(k_shaderc PRIVATE /W0 /std:clatest /experimental:c11atomics)
else()
    set_target_properties(k_shaderc PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
    target_compile_options(k_shaderc PRIVATE -w)
endif()
target_link_libraries(k PRIVATE k_shaderc)

# The in-process C backend: TinyCC, vendored under third_party/tinycc and
# built into every compiler (cmake/libtcc.cmake).
include(cmake/libtcc.cmake)
target_link_libraries(k PRIVATE libtcc)
add_dependencies(k tcc_runtime)

# `value` as a C string literal in `out`. Paths reach the compiler as such
# literals in generated headers rather than as -D definitions, which CMake
# drops when they hold a '#' (a checkout under ~/#projects, say).
function(k_c_string out value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(${out} "\"${value}\"" PARENT_SCOPE)
endfunction()

# A packager sets this to where it installs tcclib/, so the binary names no
# build directory. A tcclib/ beside the compiler still wins (src/jit.h).
set(K_TCC_LIB_PATH "${TCC_STAGE}" CACHE PATH "TinyCC's runtime directory")
# Likewise where it installs stdlib/. The compiler looks there after the
# directories around its executable (StdlibDirs in src/main.cpp); empty, it
# names no such directory.
set(K_STDLIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/stdlib" CACHE PATH "The standard library's directory")
k_c_string(k_tcc_lib_path "${K_TCC_LIB_PATH}")
k_c_string(k_stdlib_path "${K_STDLIB_PATH}")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/build_paths.h" CONTENT
     "// Generated by CMakeLists.txt: the paths this compiler was configured with.\n#pragma once\n\n#define K_TCC_LIB_PATH @k_tcc_lib_path@\n#define K_STDLIB_PATH @k_stdlib_path@\n"
     @ONLY)
target_include_directories(k PRIVATE "${CMAKE_BINARY_DIR}/generated")

# C headers included explicitly by a program see the same small host interface
# as generated C. These paths precede TinyCC's own compiler headers.
foreach(header stdint.h string.h stdlib.h math.h)
    configure_file(src/runtime/jit_c.h "${TCC_STAGE}/klang/include/${header}" COPYONLY)
endforeach()

# Register the functions declared by the separate-runtime interface. Reading
# the headers at configure time keeps this table in step with that interface.
set(jit_symbols "")
foreach(header runtime.h runtime_ext.h)
    set(path "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/${header}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(STRINGS "${path}" declarations REGEX "^K_API ")
    foreach(declaration IN LISTS declarations)
        if(declaration MATCHES "(k_[A-Za-z0-9_]+)\\(")
            string(APPEND jit_symbols "K_JIT_SYMBOL(${CMAKE_MATCH_1})\n")
        endif()
    endforeach()
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/jit_symbols.inc" CONTENT "${jit_symbols}" @ONLY)
target_sources(k PRIVATE src/jit_runtime.c src/jit_runtime.h)
target_include_directories(k PRIVATE "${CMAKE_BINARY_DIR}")
set_property(TARGET k PROPERTY C_STANDARD 11)

# The blessed packages K_BUILTIN_LIBS names (src/blessed.txt; `all`, or
# a list of their names), compiled into the compiler (cmake/package.cmake),
# so that their programs run with nothing else installed. Empty by default:
# the compiler builds alone, and a program importing one of them builds its
# native part into K_HOME on first use.
set(K_BUILTIN_LIBS "" CACHE STRING
    "Blessed packages to compile into the compiler: all, or a list of their names")
include(cmake/package.cmake)
k_builtin_packages("${K_BUILTIN_LIBS}")
if(MSVC AND TARGET k_gfx)
    # The system DLLs static SDL brings into klang with the gfx package:
    # delay-loaded, so that a compiler run which never opens a window does
    # not load them.
    foreach(dll IN ITEMS user32 gdi32 winmm imm32 ole32 oleaut32 version setupapi shell32)
        target_link_options(k PRIVATE "/DELAYLOAD:${dll}.dll")
    endforeach()
    target_link_libraries(k PRIVATE delayimp)
endif()

if(MSVC)
    # /bigobj: the compiler is one translation unit of header-inline
    # functions, past the object format's default COMDAT section limit.
    target_compile_options(k PRIVATE /W4 /permissive- /utf-8 /EHsc /bigobj)
    target_compile_definitions(k PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # -Wmissing-field-initializers (in -Wextra) fires on every aggregate
    # initialised with a prefix of its fields, which is deliberate throughout:
    # the rest are value-initialised.
    target_compile_options(k PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

option(K_BUILD_LSP_TESTS "Build deterministic language server tests" OFF)
if(K_BUILD_LSP_TESTS)
    get_target_property(k_lsp_sources k SOURCES)
    list(REMOVE_ITEM k_lsp_sources src/main.cpp)
    add_executable(k_lsp_test test/lsp_server.cpp ${k_lsp_sources})
    foreach(prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS LINK_LIBRARIES)
        set_property(TARGET k_lsp_test PROPERTY ${prop} "$<TARGET_PROPERTY:k,${prop}>")
    endforeach()
    add_dependencies(k_lsp_test tcc_runtime)
    enable_testing()
    add_test(NAME lsp_call_hierarchy COMMAND k_lsp_test
        "${CMAKE_CURRENT_SOURCE_DIR}/stdlib" "${CMAKE_CURRENT_BINARY_DIR}/lsp_calls.k")
endif()
