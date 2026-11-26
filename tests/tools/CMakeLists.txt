pkg_check_modules(TEST_TOOL_DEPS REQUIRED IMPORTED_TARGET wayland-client xkbcommon)

add_executable(tetra-sendkeys sendkeys.cpp)
tetra_wayland_protocols(tetra-sendkeys CLIENT
    "${WLR_PROTOCOLS_DIR}/virtual-keyboard-unstable-v1.xml"
)
target_link_libraries(tetra-sendkeys PRIVATE PkgConfig::TEST_TOOL_DEPS tetra_warnings)

add_executable(tetra-pointer-storm pointer_storm.cpp)
tetra_wayland_protocols(tetra-pointer-storm CLIENT
    "${WLR_PROTOCOLS_DIR}/wlr-virtual-pointer-unstable-v1.xml"
)
target_link_libraries(tetra-pointer-storm PRIVATE PkgConfig::TEST_TOOL_DEPS tetra_warnings)

add_executable(tetra-pointer pointer.cpp)
tetra_wayland_protocols(tetra-pointer CLIENT
    "${WLR_PROTOCOLS_DIR}/wlr-virtual-pointer-unstable-v1.xml"
)
target_link_libraries(tetra-pointer PRIVATE PkgConfig::TEST_TOOL_DEPS tetra_warnings)

find_package(sdbus-c++ 2 REQUIRED)
add_executable(tetra-tray-item tray_item.cpp)
target_link_libraries(tetra-tray-item PRIVATE SDBusCpp::sdbus-c++ tetra_warnings)

add_executable(tetra-foreign-parent foreign_parent.cpp)
target_link_libraries(tetra-foreign-parent PRIVATE tetra_toolkit tetra_warnings)
