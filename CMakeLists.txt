cmake_minimum_required(VERSION 3.28)
project(tetra VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- dependencies ---------------------------------------------------------------
# wlroots 0.20 and the libraries it needs come from the system where it has
# them; scripts/build-deps.sh builds whatever it lacks into this prefix
# (empty or missing when the system has everything). After running it again,
# reconfigure with --fresh: pkg-config results are cached.
set(TETRA_DEPS_PREFIX "${CMAKE_SOURCE_DIR}/.deps/prefix" CACHE PATH
    "Prefix holding the dependencies built by scripts/build-deps.sh")
file(GLOB TETRA_DEPS_LIBS "${TETRA_DEPS_PREFIX}/lib/lib*.so.*"
                          "${TETRA_DEPS_PREFIX}/lib/libwlroots-*.so")

# pkg-config must see the prefix before the system copies.
set(ENV{PKG_CONFIG_PATH}
    "${TETRA_DEPS_PREFIX}/lib/pkgconfig:${TETRA_DEPS_PREFIX}/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")

# Packages (.deb, Arch) ship the built libraries, if any, in a private
# directory, <libdir>/tetra, which the installed binaries use instead of the
# prefix; nothing else on the system sees them.
option(TETRA_BUNDLE_DEPS "Install the libraries built by build-deps.sh into <libdir>/tetra (packages)" OFF)
include(GNUInstallDirs)
if(TETRA_DEPS_LIBS)
    # Old-style DT_RPATH (not RUNPATH) so it also applies to the dependencies
    # of our dependencies: the whole process uses the built copies of
    # libwayland, libdrm and xkbcommon instead of mixing them with older
    # system ones.
    add_link_options("LINKER:--disable-new-dtags")
    if(TETRA_BUNDLE_DEPS)
        set(TETRA_PRIVATE_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}/tetra")
        set(CMAKE_BUILD_RPATH "${TETRA_DEPS_PREFIX}/lib") # for the unit tests
        set(CMAKE_INSTALL_RPATH "${TETRA_PRIVATE_LIBDIR}")
        # Runtime files only (sonames and their targets), no headers or .pc files.
        install(FILES ${TETRA_DEPS_LIBS} DESTINATION "${TETRA_PRIVATE_LIBDIR}")
    else()
        # Build and install trees use the same path, so skip CMake's
        # install-time rewriting (and the ':' padding it adds, which ld.so
        # reads as "cwd").
        set(CMAKE_INSTALL_RPATH "${TETRA_DEPS_PREFIX}/lib")
        set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
    endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(WLROOTS IMPORTED_TARGET wlroots-0.20)
if(NOT WLROOTS_FOUND)
    message(FATAL_ERROR "wlroots 0.20 not found, neither on the system nor in ${TETRA_DEPS_PREFIX}.\n"
                        "Run scripts/build-deps.sh first.")
endif()
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
add_subdirectory(src/audio)
add_subdirectory(src/shell)
add_subdirectory(src/apps)
add_subdirectory(src/portal)
add_subdirectory(src/greeter)

option(TETRA_BUILD_TEST_TOOLS "Build helpers for the headless tests" ON)
if(TETRA_BUILD_TEST_TOOLS)
    add_subdirectory(tests/tools)
    add_subdirectory(tests/unit)
endif()
