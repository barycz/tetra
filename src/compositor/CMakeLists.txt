pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo cairo)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(LIBJPEG REQUIRED IMPORTED_TARGET libjpeg)

# Desktop pictures (JPEG, PNG): a library of its own for the unit tests.
add_library(tetra_picture STATIC picture.cpp)
target_include_directories(tetra_picture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tetra_picture PUBLIC PkgConfig::CAIRO PRIVATE PkgConfig::LIBJPEG tetra_warnings)

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
    wallpaper.cpp
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
    tetra_picture
)

install(TARGETS tetra)
