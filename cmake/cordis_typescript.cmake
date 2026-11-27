include(FetchContent)

FetchContent_Declare(k_quickjs
    URL https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz
    URL_HASH SHA256=b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a)
FetchContent_Declare(k_typescript
    URL https://registry.npmjs.org/typescript/-/typescript-5.9.3.tgz
    URL_HASH SHA256=10e108c9cf7d5f2879053dff18515fb405abf2ccef63eaaf017d9c571687a1d3)
FetchContent_MakeAvailable(k_quickjs k_typescript)

add_library(k_cordis_quickjs STATIC
    "${k_quickjs_SOURCE_DIR}/quickjs.c"
    "${k_quickjs_SOURCE_DIR}/dtoa.c"
    "${k_quickjs_SOURCE_DIR}/libregexp.c"
    "${k_quickjs_SOURCE_DIR}/libunicode.c"
    "${k_quickjs_SOURCE_DIR}/cutils.c")
set_target_properties(k_cordis_quickjs PROPERTIES
    C_STANDARD 11 C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
# Expose just the public header. On case-insensitive filesystems the source
# archive's VERSION file shadows the C++ standard library's <version>.
set(k_quickjs_headers "${CMAKE_CURRENT_BINARY_DIR}/generated/cordis-quickjs")
configure_file("${k_quickjs_SOURCE_DIR}/quickjs.h" "${k_quickjs_headers}/quickjs.h" COPYONLY)
file(READ "${k_quickjs_SOURCE_DIR}/LICENSE" k_quickjs_license)
file(CONFIGURE OUTPUT "${k_quickjs_headers}/quickjs_license.h" CONTENT [=[
#pragma once
inline constexpr char quickjs_license[] = R"K_CORDIS_LICENSE(@k_quickjs_license@)K_CORDIS_LICENSE";
]=] @ONLY)
target_include_directories(k_cordis_quickjs SYSTEM PUBLIC "${k_quickjs_headers}")
target_compile_definitions(k_cordis_quickjs PRIVATE _GNU_SOURCE CONFIG_VERSION="2026-06-04")
target_link_libraries(k_cordis_quickjs PRIVATE Threads::Threads)
if(UNIX)
    target_link_libraries(k_cordis_quickjs PRIVATE m)
endif()

foreach(part IN ITEMS transform runtime frontend project)
    add_library(k_cordis_typescript_${part} MODULE "src/cordis/typescript_${part}.cpp")
    target_link_libraries(k_cordis_typescript_${part} PRIVATE k_cordis_quickjs)
endforeach()

add_executable(k-cordis-ts src/cordis/typescript_driver.cpp src/cordis/graph.cpp
    src/content_digest.cpp src/shared_library.cpp)
target_link_libraries(k-cordis-ts PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
add_custom_target(cordis-typescript DEPENDS k-cordis-ts
    k_cordis_typescript_frontend k_cordis_typescript_project k_cordis_typescript_transform k_cordis_typescript_runtime)

set(K_CORDIS_TYPESCRIPT_COMPILER "${k_typescript_SOURCE_DIR}/lib/typescript.js")
install(TARGETS k_cordis_typescript_transform k_cordis_typescript_runtime
                k_cordis_typescript_frontend k_cordis_typescript_project
        LIBRARY DESTINATION lib/cordis COMPONENT cordis-typescript EXCLUDE_FROM_ALL
        RUNTIME DESTINATION lib/cordis COMPONENT cordis-typescript EXCLUDE_FROM_ALL)
install(TARGETS k-cordis-ts RUNTIME DESTINATION bin COMPONENT cordis-typescript EXCLUDE_FROM_ALL)
install(FILES src/cordis/typescript_project.h src/cordis/diagnostics.h
        DESTINATION include/cordis COMPONENT cordis-typescript EXCLUDE_FROM_ALL)
install(FILES "${K_CORDIS_TYPESCRIPT_COMPILER}" "${k_typescript_SOURCE_DIR}/LICENSE.txt"
              "${k_typescript_SOURCE_DIR}/ThirdPartyNoticeText.txt"
        DESTINATION share/cordis/typescript COMPONENT cordis-typescript EXCLUDE_FROM_ALL)
install(FILES "${k_quickjs_SOURCE_DIR}/LICENSE"
        DESTINATION share/cordis/quickjs COMPONENT cordis-typescript EXCLUDE_FROM_ALL)
