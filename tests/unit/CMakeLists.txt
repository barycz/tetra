pkg_check_modules(TEST_LIBJPEG REQUIRED IMPORTED_TARGET libjpeg) # picture_test writes JPEGs

add_executable(tetra-unit-tests config_edit_test.cpp greeter_test.cpp monitor_test.cpp picture_test.cpp
               portal_test.cpp)
target_link_libraries(tetra-unit-tests PRIVATE tetra_core tetra_greeter_data tetra_monitor_data tetra_picture
                      tetra_portal_data PkgConfig::TEST_LIBJPEG tetra_warnings)
