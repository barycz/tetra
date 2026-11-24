cmake_minimum_required(VERSION 3.28)
project(tetra VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- vendored dependencies (scripts/build-deps.sh) -----------------------------
set(TETRA_DEPS_PREFIX "${CMAKE_SOURCE_DIR}/.deps/prefix" CACHE PATH
    "Prefix holding the vendored wayland/wlroots builds")
if(NOT EXISTS "${TETRA_DEPS_PREFIX}/lib/pkgconfig/wlroots-0.20.pc")
    message(FATAL_ERROR "Vendored dependencies not found in ${TETRA_DEPS_PREFIX}.\n"
                        "Run scripts/build-deps.sh first.")
endif()

# pkg-config must see the prefix before the system copies.
set(ENV{PKG_CONFIG_PATH}
    "${TETRA_DEPS_PREFIX}/lib/pkgconfig:${TETRA_DEPS_PREFIX}/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")

# Old-style DT_RPATH (not RUNPATH) so it also applies to the dependencies of
# our dependencies: the whole process uses the prefix copies of libwayland,
# libdrm, pixman and xkbcommon instead of mixing them with older system ones.
# Build and install trees use the same path, so skip CMake's install-time
# rewriting (and the ':' padding it adds, which ld.so reads as "cwd").
set(CMAKE_INSTALL_RPATH "${TETRA_DEPS_PREFIX}/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
add_link_options("LINKER:--disable-new-dtags")

find_package(PkgConfig REQUIRED)
pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.20)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

# --- shared compile settings ---------------------------------------------------
# Our C++ only; generated protocol C code and vendored libraries are exempt.
add_library(tetra_warnings INTERFACE)
target_compile_options(tetra_warnings INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
    -Wold-style-cast -Woverloaded-virtual -Wnull-dereference -Wimplicit-fallthrough>)

option(TETRA_WERROR "Treat warnings as errors" OFF)
if(TETRA_WERROR)
    target_compile_options(tetra_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
endif()

include(cmake/WaylandProtocols.cmake)

add_subdirectory(src/core)
add_subdirectory(src/compositor)
add_subdirectory(src/toolkit)
add_subdirectory(src/shell)
add_subdirectory(src/apps)

option(TETRA_BUILD_TEST_TOOLS "Build helpers for the headless tests" ON)
if(TETRA_BUILD_TEST_TOOLS)
    add_subdirectory(tests/tools)
    add_subdirectory(tests/unit)
endif()
