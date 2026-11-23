pkg_check_modules(SHELL_DEPS REQUIRED IMPORTED_TARGET libpulse)

add_executable(tetra-shell
    audio.cpp
    bar.cpp
    brightness.cpp
    launcher.cpp
    main.cpp
    notifications.cpp
    osd.cpp
    shell.cpp
    tray.cpp
    volume_popup.cpp
)
tetra_wayland_protocols(tetra-shell CLIENT
    "${TETRA_PROTOCOLS_DIR}/tetra-shell-unstable-v1.xml"
)
target_link_libraries(tetra-shell PRIVATE tetra_toolkit tetra_warnings PkgConfig::SHELL_DEPS)

install(TARGETS tetra-shell)
