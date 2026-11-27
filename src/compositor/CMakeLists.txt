pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo cairo)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)

add_executable(tetra
    animation.cpp
    cairo_buffer.cpp
    decoration.cpp
    input.cpp
    keyboard.cpp
    layer_surface.cpp
    main.cpp
    output.cpp
    placement.cpp
    pointer.cpp
    popup.cpp
    server.cpp
    session_lock.cpp
    shell_protocol.cpp
    taskbar.cpp
    view.cpp
    wm.cpp
    xdg_view.cpp
    xwayland_view.cpp
    zone_overlay.cpp
)

# wlr_layer_shell_v1.h includes the generated server header.
tetra_wayland_protocols(tetra SERVER
    "${WLR_PROTOCOLS_DIR}/wlr-layer-shell-unstable-v1.xml"
    "${WLR_PROTOCOLS_DIR}/wlr-output-power-management-unstable-v1.xml"
)
tetra_wayland_protocols(tetra SERVER_CODE
    "${TETRA_PROTOCOLS_DIR}/tetra-shell-unstable-v1.xml"
)

target_compile_definitions(tetra PRIVATE WLR_USE_UNSTABLE)
target_link_libraries(tetra PRIVATE
    tetra_core
    tetra_warnings
    PkgConfig::WLROOTS
    PkgConfig::WAYLAND_SERVER
    PkgConfig::XKBCOMMON
    PkgConfig::PANGOCAIRO
    PkgConfig::LIBINPUT
    PkgConfig::LIBDRM
)

install(TARGETS tetra)
