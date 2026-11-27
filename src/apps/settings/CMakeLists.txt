add_executable(tetra-settings
    display_page.cpp
    displays.cpp
    main.cpp
    pages.cpp
    settings.cpp
    sound_page.cpp
    xkb.cpp
    zones_page.cpp
)
target_compile_definitions(tetra-settings PRIVATE TETRA_VERSION="${PROJECT_VERSION}")
target_link_libraries(tetra-settings PRIVATE tetra_audio tetra_file_dialog tetra_toolkit tetra_warnings)

install(TARGETS tetra-settings)
install(FILES "${CMAKE_SOURCE_DIR}/data/applications/tetra-settings.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
