# Generate Wayland protocol bindings with wayland-scanner.
#
#   tetra_wayland_protocols(<target> SERVER|SERVER_CODE|CLIENT <xml>...)
#
# SERVER       server headers only (for protocols wlroots implements).
# SERVER_CODE  server headers plus interface code (for tetra's own protocols).
# CLIENT       client headers plus interface code.
# Headers land in a per-target include directory.

pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(WLR_PROTOCOLS_DIR "${CMAKE_SOURCE_DIR}/third_party/wlroots/protocol")
set(TETRA_PROTOCOLS_DIR "${CMAKE_SOURCE_DIR}/protocols")

function(tetra_wayland_protocols target kind)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/protocols")
    file(MAKE_DIRECTORY "${out_dir}")
    if(kind STREQUAL "CLIENT")
        set(header_mode client-header)
    elseif(kind STREQUAL "SERVER" OR kind STREQUAL "SERVER_CODE")
        set(header_mode server-header)
    else()
        message(FATAL_ERROR "tetra_wayland_protocols: kind must be SERVER, SERVER_CODE or CLIENT")
    endif()

    foreach(xml IN LISTS ARGN)
        get_filename_component(name "${xml}" NAME_WE)
        set(header "${out_dir}/${name}-protocol.h")
        if(kind STREQUAL "SERVER")
            add_custom_command(
                OUTPUT "${header}"
                COMMAND "${WAYLAND_SCANNER}" ${header_mode} "${xml}" "${header}"
                DEPENDS "${xml}" VERBATIM)
            target_sources(${target} PRIVATE "${header}")
        else()
            set(code "${out_dir}/${name}-protocol.c")
            add_custom_command(
                OUTPUT "${header}" "${code}"
                COMMAND "${WAYLAND_SCANNER}" ${header_mode} "${xml}" "${header}"
                COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${code}"
                DEPENDS "${xml}" VERBATIM)
            target_sources(${target} PRIVATE "${header}" "${code}")
        endif()
    endforeach()
    target_include_directories(${target} SYSTEM PUBLIC "${out_dir}")
endfunction()
