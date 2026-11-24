pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)

# Sound devices for the shell and Settings (PulseAudio API).
add_library(tetra_audio STATIC
    audio.cpp
)
target_include_directories(tetra_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tetra_audio
    PUBLIC tetra_toolkit
    PRIVATE tetra_warnings PkgConfig::PULSE
)
