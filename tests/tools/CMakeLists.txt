pkg_check_modules(SENDKEYS_DEPS REQUIRED IMPORTED_TARGET wayland-client xkbcommon)

add_executable(tetra-sendkeys sendkeys.cpp)
tetra_wayland_protocols(tetra-sendkeys CLIENT
    "${WLR_PROTOCOLS_DIR}/virtual-keyboard-unstable-v1.xml"
)
target_link_libraries(tetra-sendkeys PRIVATE PkgConfig::SENDKEYS_DEPS tetra_warnings)
