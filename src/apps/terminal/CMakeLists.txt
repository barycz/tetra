pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)

add_executable(tetra-terminal
    input.cpp
    main.cpp
    render.cpp
    terminal.cpp
)
target_link_libraries(tetra-terminal PRIVATE tetra_toolkit tetra_warnings PkgConfig::VTERM util)

install(TARGETS tetra-terminal)
install(FILES "${CMAKE_SOURCE_DIR}/data/icons/hicolor/scalable/apps/tetra-terminal.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
install(FILES "${CMAKE_SOURCE_DIR}/data/applications/tetra-terminal.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
