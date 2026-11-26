# The /proc readers, without the UI (also used by the unit tests).
add_library(tetra_monitor_data STATIC procfs.cpp sampler.cpp)
target_include_directories(tetra_monitor_data PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tetra_monitor_data PRIVATE tetra_warnings)

add_executable(tetra-monitor
    main.cpp
    monitor.cpp
    processes_page.cpp
    resources_page.cpp
)
target_link_libraries(tetra-monitor PRIVATE tetra_monitor_data tetra_toolkit tetra_warnings)

install(TARGETS tetra-monitor)
install(FILES "${CMAKE_SOURCE_DIR}/data/applications/tetra-monitor.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
