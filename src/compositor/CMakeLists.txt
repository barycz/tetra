add_executable(tetra
    keyboard.cpp
    main.cpp
    output.cpp
    server.cpp
    toplevel.cpp
)

target_compile_definitions(tetra PRIVATE WLR_USE_UNSTABLE)
target_link_libraries(tetra PRIVATE
    tetra_warnings
    PkgConfig::WLROOTS
    PkgConfig::WAYLAND_SERVER
    PkgConfig::XKBCOMMON
)

install(TARGETS tetra)
