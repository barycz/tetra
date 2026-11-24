# libpam0g-dev's pam.pc requires libaudit's .pc for static linking only.
find_library(PAM_LIBRARY pam REQUIRED)
find_path(PAM_INCLUDE_DIR security/pam_appl.h REQUIRED)

add_executable(tetra-shell
    auth.cpp
    bar.cpp
    brightness.cpp
    idle.cpp
    launcher.cpp
    lock_screen.cpp
    logind.cpp
    main.cpp
    notifications.cpp
    osd.cpp
    polkit_agent.cpp
    screenshot.cpp
    shell.cpp
    sni.cpp
    tray.cpp
    tray_menu.cpp
    volume_popup.cpp
)
tetra_wayland_protocols(tetra-shell CLIENT
    "${TETRA_PROTOCOLS_DIR}/tetra-shell-unstable-v1.xml"
)
target_include_directories(tetra-shell SYSTEM PRIVATE "${PAM_INCLUDE_DIR}")
target_link_libraries(tetra-shell PRIVATE tetra_audio tetra_toolkit tetra_warnings "${PAM_LIBRARY}")

install(TARGETS tetra-shell)
# PAM only reads /etc/pam.d: with another prefix this lands elsewhere and
# PAM falls back to "other" (fine on Debian).
include(GNUInstallDirs)
install(FILES "${CMAKE_SOURCE_DIR}/data/pam.d/tetra"
        DESTINATION "${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d")
