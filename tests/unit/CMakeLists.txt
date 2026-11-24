add_executable(tetra-unit-tests config_edit_test.cpp)
target_link_libraries(tetra-unit-tests PRIVATE tetra_core tetra_warnings)
