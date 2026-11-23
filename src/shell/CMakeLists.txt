add_executable(tetra-shell
    bar.cpp
    launcher.cpp
    main.cpp
    notifications.cpp
    shell.cpp
    tray.cpp
)
tetra_wayland_protocols(tetra-shell CLIENT
    "${TETRA_PROTOCOLS_DIR}/tetra-shell-unstable-v1.xml"
)
target_link_libraries(tetra-shell PRIVATE tetra_toolkit tetra_warnings)

install(TARGETS tetra-shell)
