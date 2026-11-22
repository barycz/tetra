# Generate Wayland protocol bindings with wayland-scanner.
#
#   tetra_wayland_protocols(<target> SERVER|CLIENT <xml>...)
#
# SERVER generates <name>-protocol.h server headers only (wlroots provides the
# implementations). CLIENT generates client headers plus the interface code,
# compiled into <target>. Headers land in a per-target include directory.

pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(WLR_PROTOCOLS_DIR "${CMAKE_SOURCE_DIR}/third_party/wlroots/protocol")

function(tetra_wayland_protocols target kind)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/protocols")
    file(MAKE_DIRECTORY "${out_dir}")
    foreach(xml IN LISTS ARGN)
        get_filename_component(name "${xml}" NAME_WE)
        set(header "${out_dir}/${name}-protocol.h")
        if(kind STREQUAL "SERVER")
            add_custom_command(
                OUTPUT "${header}"
                COMMAND "${WAYLAND_SCANNER}" server-header "${xml}" "${header}"
                DEPENDS "${xml}" VERBATIM)
            target_sources(${target} PRIVATE "${header}")
        elseif(kind STREQUAL "CLIENT")
            set(code "${out_dir}/${name}-protocol.c")
            add_custom_command(
                OUTPUT "${header}" "${code}"
                COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${header}"
                COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${code}"
                DEPENDS "${xml}" VERBATIM)
            target_sources(${target} PRIVATE "${header}" "${code}")
        else()
            message(FATAL_ERROR "tetra_wayland_protocols: kind must be SERVER or CLIENT")
        endif()
    endforeach()
    target_include_directories(${target} SYSTEM PUBLIC "${out_dir}")
endfunction()
