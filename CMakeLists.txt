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
    src/content_digest.cpp
    src/content_digest.h
    src/cordis/graph.cpp
    src/cordis/graph.h
    src/process.cpp
    src/shared_library.cpp
    src/version.cpp
    src/process.h
    src/shared_library.h
    src/progress.cpp
    src/progress.h
    src/buildfile.h
    src/toolchain.h
    src/artifact_cache.h
    src/build.h
    src/packages.h
    src/program.h
    src/paths.h
    src/json.h
    src/native.h
    src/cordis/diagnostics.h
    src/cordis/service.h
    src/cordis_diagnostics.h
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
    src/typecheck_task_liveness.h
    src/typecheck_ownership.h
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
    src/codegen_task_flow.h
    src/codegen_emit.h
    src/codegen_stacks.h
    src/codegen_merge.h
    src/codegen_nodes.h
    src/runtime_inline.h
    src/jit.h
    src/runtime/runtime.h
    src/runtime/runtime_impl.h
    src/runtime/runtime_threads.h
    src/runtime/runtime_tasks.h
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
target_link_libraries(k PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
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
option(K_BUILTIN_INSTALLED_ONLY
    "Resolve compiled-in packages beside the installed compiler" OFF)
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

option(K_BUILD_LSP_TESTS "Build deterministic language server and Cordis tests" OFF)
option(K_BUILD_CORDIS_HOST "Build the optional Cordis application host" OFF)
option(K_BUILD_CORDIS_SOURCE "Build the optional Cordis source transform driver" OFF)
option(K_BUILD_CORDIS_PYTHON "Build the optional Cordis CPython guest adapter" OFF)
option(K_BUILD_CORDIS_TYPESCRIPT "Build the optional Cordis QuickJS and TypeScript guest adapter" OFF)
option(K_BUILD_TCP_READ_PROVIDER "Build the optional Cordis TCP receive and stream providers" OFF)
if(K_BUILD_CORDIS_SOURCE OR K_BUILD_LSP_TESTS)
    add_executable(k-cordis-source src/cordis/source_driver.cpp src/cordis/graph.cpp
        src/content_digest.cpp src/process.cpp src/progress.cpp src/shared_library.cpp)
    target_include_directories(k-cordis-source PRIVATE third_party/subprocess)
    target_link_libraries(k-cordis-source PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    if(K_BUILD_CORDIS_SOURCE)
        install(TARGETS k-cordis-source RUNTIME DESTINATION bin COMPONENT cordis-source EXCLUDE_FROM_ALL)
        install(FILES src/cordis/source_transform.h src/cordis/diagnostics.h DESTINATION include/cordis
                COMPONENT cordis-source EXCLUDE_FROM_ALL)
    endif()
endif()
if(K_BUILD_CORDIS_PYTHON)
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter Development.Embed)
    k_c_string(k_python_executable "${Python3_EXECUTABLE}")
    foreach(part IN ITEMS transform runtime)
        add_library(k_cordis_python_${part} MODULE src/cordis/python_${part}.cpp)
        target_compile_definitions(k_cordis_python_${part} PRIVATE K_CORDIS_PYTHON_EXECUTABLE=${k_python_executable})
        target_link_libraries(k_cordis_python_${part} PRIVATE Python3::Python Threads::Threads)
    endforeach()
    install(TARGETS k_cordis_python_transform k_cordis_python_runtime
            LIBRARY DESTINATION lib/cordis COMPONENT cordis-python EXCLUDE_FROM_ALL
            RUNTIME DESTINATION lib/cordis COMPONENT cordis-python EXCLUDE_FROM_ALL)
endif()
if(K_BUILD_CORDIS_TYPESCRIPT)
    include(cmake/cordis_typescript.cmake)
endif()
set(K_ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime SDK root for the optional Cordis CPU provider")
if(K_ONNXRUNTIME_ROOT)
    find_path(K_ORT_INCLUDE onnxruntime_c_api.h
        PATHS "${K_ONNXRUNTIME_ROOT}" PATH_SUFFIXES include include/onnxruntime
        NO_DEFAULT_PATH NO_CACHE REQUIRED)
    add_library(k_cordis_onnx MODULE src/cordis/onnx_inference.cpp src/shared_library.cpp)
    target_include_directories(k_cordis_onnx PRIVATE "${K_ORT_INCLUDE}")
    target_compile_definitions(k_cordis_onnx PRIVATE K_CORDIS_INFERENCE_BUILD)
    target_link_libraries(k_cordis_onnx PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()
if(K_BUILD_CORDIS_HOST OR K_BUILD_LSP_TESTS)
    add_library(k_cordis_application SHARED
        src/cordis/application.cpp src/cordis/gateway_net.cpp src/cordis/graph.cpp src/cordis/memory_budget.cpp
        src/content_digest.cpp src/shared_library.cpp)
    target_compile_definitions(k_cordis_application PRIVATE K_CORDIS_APPLICATION_BUILD)
    target_link_libraries(k_cordis_application PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    if(WIN32)
        set_target_properties(k_cordis_application PROPERTIES PREFIX "")
        target_link_libraries(k_cordis_application PRIVATE ws2_32)
    endif()
endif()
if(K_BUILD_TCP_READ_PROVIDER OR K_BUILD_LSP_TESTS)
    add_library(k_cordis_tcp_read MODULE src/cordis/tcp_read.cpp src/cordis/tcp_stream.cpp)
    target_include_directories(k_cordis_tcp_read PRIVATE src)
    target_compile_definitions(k_cordis_tcp_read PRIVATE K_CORDIS_TCP_READ_SHARED)
    target_link_libraries(k_cordis_tcp_read PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(k_cordis_tcp_read PRIVATE ws2_32)
    endif()
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND K_BUILD_CORDIS_HOST AND
   K_BUILD_TCP_READ_PROVIDER AND K_ONNXRUNTIME_ROOT)
    add_executable(k-inference-supervisor src/cordis/gateway_supervisor.cpp)
    install(TARGETS k-inference-supervisor RUNTIME DESTINATION . COMPONENT cloud EXCLUDE_FROM_ALL)
    install(TARGETS k_cordis_application k_cordis_tcp_read k_cordis_onnx
            LIBRARY DESTINATION lib COMPONENT cloud EXCLUDE_FROM_ALL)
    install(FILES "${K_ONNXRUNTIME_ROOT}/lib/libonnxruntime.so"
                  "${K_ONNXRUNTIME_ROOT}/lib/libonnxruntime_providers_shared.so"
            DESTINATION lib COMPONENT cloud EXCLUDE_FROM_ALL)
    install(FILES "${K_ONNXRUNTIME_ROOT}/LICENSE" "${K_ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt"
            DESTINATION licenses/onnxruntime COMPONENT cloud EXCLUDE_FROM_ALL)
endif()
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
    foreach(provider IN ITEMS a b failed incompatible)
        set(abi 1)
        set(start_status 0)
        if(provider STREQUAL "failed")
            set(start_status 9)
        elseif(provider STREQUAL "incompatible")
            set(abi 2)
        endif()
        add_library(k_cordis_test_${provider} MODULE test/cordis/diagnostics.c)
        target_include_directories(k_cordis_test_${provider} PRIVATE src)
        target_compile_definitions(k_cordis_test_${provider} PRIVATE
            "K_CORDIS_TEST_PROVIDER=\"${provider}\""
            "K_CORDIS_TEST_ABI=${abi}" "K_CORDIS_TEST_START_STATUS=${start_status}")
        add_dependencies(k_lsp_test k_cordis_test_${provider})
    endforeach()
    add_test(NAME lsp_cordis_diagnostics COMMAND "${CMAKE_COMMAND}" -E env
        "K_CORDIS_TEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/cordis-test-events.log"
        "$<TARGET_FILE:k_lsp_test>" --cordis
        "${CMAKE_CURRENT_SOURCE_DIR}/stdlib" "${CMAKE_CURRENT_BINARY_DIR}"
        "$<TARGET_FILE:k_cordis_test_a>" "$<TARGET_FILE:k_cordis_test_b>"
        "$<TARGET_FILE:k_cordis_test_failed>" "$<TARGET_FILE:k_cordis_test_incompatible>")

    foreach(provider IN ITEMS shared failed incompatible)
        set(abi 1)
        set(start_status 0)
        if(provider STREQUAL "failed")
            set(start_status 9)
        elseif(provider STREQUAL "incompatible")
            set(abi 2)
        endif()
        add_library(k_cordis_value_${provider} MODULE test/cordis/value.c)
        target_compile_definitions(k_cordis_value_${provider} PRIVATE
            K_CORDIS_TEST_SHARED "K_CORDIS_TEST_PROVIDER=\"${provider}\"" K_CORDIS_TEST_VALUE=29
            "K_CORDIS_TEST_ABI=${abi}" "K_CORDIS_TEST_START_STATUS=${start_status}")
    endforeach()

    add_library(k_cordis_value_linked STATIC test/cordis/value.c)
    target_compile_definitions(k_cordis_value_linked PRIVATE
        K_CORDIS_TEST_PROVIDER=\"linked\" K_CORDIS_TEST_VALUE=17
        K_CORDIS_TEST_ABI=1 K_CORDIS_TEST_START_STATUS=0)

    foreach(mode IN ITEMS static native)
        add_executable(k_cordis_${mode}_test test/cordis/host.cpp)
        target_include_directories(k_cordis_${mode}_test PRIVATE src)
        target_link_libraries(k_cordis_${mode}_test PRIVATE k_cordis_value_linked)
    endforeach()
    target_sources(k_cordis_native_test PRIVATE src/shared_library.cpp)
    target_compile_definitions(k_cordis_native_test PRIVATE K_CORDIS_TEST_DYNAMIC)
    target_link_libraries(k_cordis_native_test PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(k_cordis_native_test k_cordis_value_shared k_cordis_value_failed
        k_cordis_value_incompatible k_cordis_test_a)
    add_test(NAME cordis_static_host COMMAND "${CMAKE_COMMAND}" -E env
        "K_CORDIS_TEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/cordis-static-events.log"
        "$<TARGET_FILE:k_cordis_static_test>")
    add_test(NAME cordis_native_host COMMAND "${CMAKE_COMMAND}" -E env
        "K_CORDIS_TEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/cordis-native-events.log"
        "$<TARGET_FILE:k_cordis_native_test>" "$<TARGET_FILE:k_cordis_value_shared>"
        "$<TARGET_FILE:k_cordis_value_failed>" "$<TARGET_FILE:k_cordis_value_incompatible>"
        "$<TARGET_FILE:k_cordis_test_a>")

    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_library(k_cordis_source_fixture MODULE test/cordis/source_transform.c)
        target_include_directories(k_cordis_source_fixture PRIVATE src)
        add_test(NAME cordis_source_transform COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_source.py"
            "$<TARGET_FILE:k-cordis-source>" "$<TARGET_FILE:k>" "$<TARGET_FILE:k_cordis_source_fixture>"
            "$<TARGET_FILE:k_cordis_test_incompatible>")
        set_tests_properties(cordis_source_transform PROPERTIES TIMEOUT 120)
        if(TARGET k_cordis_python_transform)
            add_test(NAME cordis_python_guest COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_python.py"
                "$<TARGET_FILE:k-cordis-source>" "$<TARGET_FILE:k>"
                "$<TARGET_FILE:k_cordis_python_transform>" "$<TARGET_FILE:k_cordis_python_runtime>")
            set_tests_properties(cordis_python_guest PROPERTIES TIMEOUT 120)
        endif()
        if(TARGET k_cordis_typescript_transform)
            add_test(NAME cordis_typescript_guest COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_typescript.py"
                "$<TARGET_FILE:k-cordis-source>" "$<TARGET_FILE:k>"
                "$<TARGET_FILE:k_cordis_typescript_transform>" "$<TARGET_FILE:k_cordis_typescript_runtime>"
                "${K_CORDIS_TYPESCRIPT_COMPILER}")
            set_tests_properties(cordis_typescript_guest PROPERTIES TIMEOUT 180)
            add_test(NAME cordis_typescript_project COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_typescript_project.py"
                "$<TARGET_FILE:k-cordis-ts>" "$<TARGET_FILE:k>"
                "$<TARGET_FILE:k_cordis_typescript_frontend>" "$<TARGET_FILE:k_cordis_typescript_project>"
                "${K_CORDIS_TYPESCRIPT_COMPILER}")
            set_tests_properties(cordis_typescript_project PROPERTIES TIMEOUT 180)
        endif()
        add_library(k_cordis_tcp_fixture MODULE test/cordis/tcp_read_fixture.cpp
            src/cordis/tcp_read.cpp src/cordis/tcp_stream.cpp src/shared_library.cpp)
        target_include_directories(k_cordis_tcp_fixture PRIVATE src)
        target_link_libraries(k_cordis_tcp_fixture PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        if(WIN32)
            target_link_libraries(k_cordis_tcp_fixture PRIVATE ws2_32)
        endif()
        add_library(k_cordis_test_stream_impl OBJECT src/cordis/tcp_stream.cpp)
        target_include_directories(k_cordis_test_stream_impl PRIVATE src)
        target_compile_definitions(k_cordis_test_stream_impl PRIVATE
            k_cordis_tcp_stream_v1=k_test_stream_implementation)
        set_target_properties(k_cordis_test_stream_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
        foreach(provider IN ITEMS a b)
            set(capacity 1)
            if(provider STREQUAL "b")
                set(capacity 2)
            endif()
            add_library(k_cordis_app_policy_${provider} MODULE test/cordis/tcp_application_policy.c)
            target_include_directories(k_cordis_app_policy_${provider} PRIVATE src)
            target_compile_definitions(k_cordis_app_policy_${provider} PRIVATE K_CORDIS_TCP_POLICY_BUILD
                K_TEST_CORDIS_PROVIDER_ID="${provider}" K_TEST_CORDIS_MAX_CAPACITY=${capacity})
            add_library(k_cordis_app_stream_${provider} MODULE
                test/cordis/tcp_application_provider.cpp $<TARGET_OBJECTS:k_cordis_test_stream_impl>)
            target_include_directories(k_cordis_app_stream_${provider} PRIVATE src)
            target_compile_definitions(k_cordis_app_stream_${provider} PRIVATE K_TEST_CORDIS_PROVIDER_ID="${provider}")
            target_link_libraries(k_cordis_app_stream_${provider} PRIVATE Threads::Threads)
            if(WIN32)
                target_link_libraries(k_cordis_app_stream_${provider} PRIVATE ws2_32)
            endif()
        endforeach()
        add_test(NAME cordis_application_stream COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_application.py"
            "$<TARGET_FILE:k>" "$<TARGET_FILE:k_cordis_application>" "$<TARGET_FILE:k_cordis_tcp_fixture>"
            "$<TARGET_FILE:k_cordis_app_stream_a>" "$<TARGET_FILE:k_cordis_app_stream_b>"
            "$<TARGET_FILE:k_cordis_app_policy_a>" "$<TARGET_FILE:k_cordis_app_policy_b>"
            "$<TARGET_FILE:k_cordis_test_incompatible>")
        set_tests_properties(cordis_application_stream PROPERTIES TIMEOUT 180)
        if(TARGET k_cordis_onnx)
            find_file(K_ORT_TEST_LIBRARY NAMES "${CMAKE_SHARED_LIBRARY_PREFIX}onnxruntime${CMAKE_SHARED_LIBRARY_SUFFIX}"
                PATHS "${K_ONNXRUNTIME_ROOT}" PATH_SUFFIXES lib bin NO_DEFAULT_PATH NO_CACHE)
            if(K_ORT_TEST_LIBRARY)
                add_test(NAME cordis_onnx_inference COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_inference.py"
                    "$<TARGET_FILE:k>" "$<TARGET_FILE:k_cordis_application>"
                    "$<TARGET_FILE:k_cordis_onnx>" "${K_ORT_TEST_LIBRARY}"
                    "$<TARGET_FILE:k_cordis_tcp_read>" "$<TARGET_FILE:k_cordis_tcp_fixture>")
                set_tests_properties(cordis_onnx_inference PROPERTIES TIMEOUT 180)
                add_library(k_cordis_test_gateway_slow MODULE test/cordis/gateway_slow_provider.cpp)
                target_include_directories(k_cordis_test_gateway_slow PRIVATE src)
                target_compile_definitions(k_cordis_test_gateway_slow PRIVATE K_CORDIS_INFERENCE_BUILD)
                target_link_libraries(k_cordis_test_gateway_slow PRIVATE Threads::Threads)
                add_test(NAME cordis_inference_gateway COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_gateway.py"
                    "$<TARGET_FILE:k>" "$<TARGET_FILE:k_cordis_application>" "$<TARGET_FILE:k_cordis_onnx>"
                    "${K_ORT_TEST_LIBRARY}" "$<TARGET_FILE:k_cordis_test_gateway_slow>")
                set_tests_properties(cordis_inference_gateway PROPERTIES TIMEOUT 180)
            endif()
        endif()
        add_test(NAME cordis_tcp_receive COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_tcp.py"
            "$<TARGET_FILE:k>" "$<TARGET_FILE:k_cordis_tcp_read>" "$<TARGET_FILE:k_cordis_tcp_fixture>"
            "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME cordis_tcp_stream COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_tcp.py"
            "$<TARGET_FILE:k>" "$<TARGET_FILE:k_cordis_tcp_read>" "$<TARGET_FILE:k_cordis_tcp_fixture>"
            "${CMAKE_CURRENT_BINARY_DIR}" stream)
        set_tests_properties(cordis_tcp_receive cordis_tcp_stream PROPERTIES TIMEOUT 120)
        add_test(NAME lsp_cordis_protocol COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_protocol.py"
            "$<TARGET_FILE:k>" "$<TARGET_FILE:k_cordis_test_a>" "$<TARGET_FILE:k_cordis_test_b>")
        set_tests_properties(lsp_cordis_protocol PROPERTIES TIMEOUT 120)
        add_test(NAME cordis_locked_build COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test/cordis_build.py"
            "$<TARGET_FILE:k>" "$<TARGET_FILE:k_cordis_test_a>" "$<TARGET_FILE:k_cordis_test_b>"
            "$<TARGET_FILE:k_cordis_test_incompatible>" "$<TARGET_FILE:k_cordis_test_failed>")
        set_tests_properties(cordis_locked_build PROPERTIES TIMEOUT 180)
    endif()
endif()
