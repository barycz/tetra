add_executable(tetra-settings
    display_page.cpp
    displays.cpp
    zones_page.cpp
    main.cpp
    pages.cpp
    settings.cpp
    xkb.cpp
)
target_compile_definitions(tetra-settings PRIVATE TETRA_VERSION="${PROJECT_VERSION}")
target_link_libraries(tetra-settings PRIVATE tetra_toolkit tetra_warnings)

install(TARGETS tetra-settings)
install(FILES "${CMAKE_SOURCE_DIR}/data/applications/tetra-settings.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
