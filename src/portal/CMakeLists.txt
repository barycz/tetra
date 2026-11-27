include(GNUInstallDirs)

# File system helpers and settings values, without UI (also unit-tested).
add_library(tetra_portal_data STATIC appearance.cpp files.cpp)
target_include_directories(tetra_portal_data PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tetra_portal_data PUBLIC tetra_core PRIVATE tetra_warnings)

pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

# The file chooser window: the portal's, and Settings' picture chooser.
add_library(tetra_file_dialog STATIC file_dialog.cpp)
target_link_libraries(tetra_file_dialog PUBLIC tetra_portal_data tetra_toolkit PRIVATE tetra_warnings)

add_executable(tetra-portal
    access_dialog.cpp
    main.cpp
    portal.cpp
    screencast.cpp
)
target_link_libraries(tetra-portal PRIVATE tetra_file_dialog tetra_warnings PkgConfig::PIPEWIRE)

install(TARGETS tetra-portal DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")
configure_file("${CMAKE_SOURCE_DIR}/data/portal/org.freedesktop.impl.portal.desktop.tetra.service.in"
               "${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.tetra.service" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.tetra.service"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/services")
install(FILES "${CMAKE_SOURCE_DIR}/data/portal/tetra.portal"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal/portals")
install(FILES "${CMAKE_SOURCE_DIR}/data/portal/tetra-portals.conf"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal")
