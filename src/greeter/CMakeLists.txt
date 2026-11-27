find_package(nlohmann_json 3.11 REQUIRED)

# The greetd protocol and the user list, without the UI (also used by the
# unit tests).
add_library(tetra_greeter_data STATIC protocol.cpp users.cpp)
target_include_directories(tetra_greeter_data PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tetra_greeter_data PRIVATE tetra_warnings nlohmann_json::nlohmann_json)

add_executable(tetra-greeter
    greetd.cpp
    greeter.cpp
    main.cpp
)
target_link_libraries(tetra-greeter PRIVATE tetra_greeter_data tetra_toolkit tetra_warnings)

include(GNUInstallDirs)
install(TARGETS tetra-greeter)
install(FILES "${CMAKE_SOURCE_DIR}/data/wayland-sessions/tetra.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/wayland-sessions")
install(FILES "${CMAKE_SOURCE_DIR}/data/greetd/config.toml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/tetra/greetd")
