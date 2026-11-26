add_executable(tetra-unit-tests config_edit_test.cpp monitor_test.cpp portal_test.cpp)
target_link_libraries(tetra-unit-tests PRIVATE tetra_core tetra_monitor_data tetra_portal_data tetra_warnings)
