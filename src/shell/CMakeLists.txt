add_executable(tetra-shell
    bar.cpp
    main.cpp
)
target_link_libraries(tetra-shell PRIVATE tetra_toolkit tetra_warnings)

install(TARGETS tetra-shell)
