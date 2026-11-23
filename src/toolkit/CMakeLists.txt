pkg_check_modules(TOOLKIT_DEPS REQUIRED IMPORTED_TARGET
    wayland-client wayland-egl egl glesv2 xkbcommon fontconfig freetype2 cairo librsvg-2.0)

# --- Dear ImGui (vendored) -------------------------------------------------------
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    "${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp"
)
target_include_directories(imgui SYSTEM PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_compile_definitions(imgui PUBLIC
    IMGUI_ENABLE_FREETYPE          # sharper text than stb_truetype
    IMGUI_IMPL_OPENGL_ES3          # GLES 3.0, "#version 300 es"
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)
target_link_libraries(imgui PUBLIC PkgConfig::TOOLKIT_DEPS)

# --- tetra toolkit ---------------------------------------------------------------------
find_package(sdbus-c++ 2 REQUIRED)

add_library(tetra_toolkit STATIC
    app.cpp
    dbus.cpp
    icons.cpp
    keymap.cpp
    style.cpp
    texture.cpp
    window.cpp
)
tetra_wayland_protocols(tetra_toolkit CLIENT
    "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml"
    "${WAYLAND_PROTOCOLS_DIR}/stable/tablet/tablet-v2.xml"
    "${WAYLAND_PROTOCOLS_DIR}/staging/cursor-shape/cursor-shape-v1.xml"
    "${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml"
    "${WLR_PROTOCOLS_DIR}/wlr-layer-shell-unstable-v1.xml"
)
target_include_directories(tetra_toolkit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tetra_toolkit
    PUBLIC tetra_core imgui PkgConfig::TOOLKIT_DEPS SDBusCpp::sdbus-c++
    PRIVATE tetra_warnings
)
