find_package(nlohmann_json 3.11 REQUIRED)

add_library(tetra_core STATIC
    config.cpp
    file_watcher.cpp
    theme.cpp
)
target_include_directories(tetra_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tetra_core
    PUBLIC tetra_warnings
    PRIVATE nlohmann_json::nlohmann_json
)
