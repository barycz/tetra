// tetra-png-to-jpeg IN.png OUT.jpg [ORIENTATION]
//
// Writes a PNG as a JPEG for the background tests (the system's tools may
// have no JPEG writer). With an EXIF ORIENTATION (2-8) the pixels are stored
// turned the other way, as a camera would, so a viewer that honours the tag
// shows the picture exactly like the PNG, and one that ignores it does not.

#include <cairo.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <print>
#include <string_view>
#include <vector>

#include <jpeglib.h>

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::println(stderr, "usage: tetra-png-to-jpeg IN.png OUT.jpg [ORIENTATION]");
        return 2;
    }
    int orientation = 1;
    if (argc == 4) {
        const std::string_view arg = argv[3];
        if (std::from_chars(arg.data(), arg.data() + arg.size(), orientation).ec != std::errc{} || orientation < 1 ||
            orientation > 8) {
            std::println(stderr, "tetra-png-to-jpeg: ORIENTATION must be 1-8");
            return 2;
        }
    }
    cairo_surface_t* png = cairo_image_surface_create_from_png(argv[1]);
    if (cairo_surface_status(png) != CAIRO_STATUS_SUCCESS) {
        std::println(stderr, "tetra-png-to-jpeg: {}: {}", argv[1], cairo_status_to_string(cairo_surface_status(png)));
        return 1;
    }
    const int upright_w = cairo_image_surface_get_width(png);
    const int upright_h = cairo_image_surface_get_height(png);
    const int stride = cairo_image_surface_get_stride(png);
    const unsigned char* pixels = cairo_image_surface_get_data(png);
    const bool turned = orientation >= 5;
    const int width = turned ? upright_h : upright_w; // as stored
    const int height = turned ? upright_w : upright_h;

    std::FILE* file = std::fopen(argv[2], "wb");
    if (file == nullptr) {
        std::println(stderr, "tetra-png-to-jpeg: cannot write {}", argv[2]);
        return 1;
    }
    jpeg_compress_struct c{};
    jpeg_error_mgr err{};
    c.err = jpeg_std_error(&err);
    jpeg_create_compress(&c);
    jpeg_stdio_dest(&c, file);
    c.image_width = static_cast<JDIMENSION>(width);
    c.image_height = static_cast<JDIMENSION>(height);
    c.input_components = 3;
    c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, 92, TRUE);
    jpeg_start_compress(&c, TRUE);
    if (orientation != 1) {
        // APP1: "Exif", a little-endian TIFF header and IFD0 with only Orientation.
        const std::array<unsigned char, 32> exif{'E', 'x', 'i', 'f', 0, 0,                         // header
                                                 'I', 'I', 42, 0, 8, 0, 0, 0,                      // IFD0 at 8
                                                 1, 0,                                             // one entry
                                                 0x12, 0x01, 3, 0, 1, 0, 0, 0,                     // SHORT x1
                                                 static_cast<unsigned char>(orientation), 0, 0, 0, // value
                                                 0, 0, 0, 0};                                      // no next IFD
        jpeg_write_marker(&c, JPEG_APP0 + 1, exif.data(), static_cast<unsigned>(exif.size()));
    }
    std::vector<unsigned char> row(static_cast<std::size_t>(width) * 3);
    while (c.next_scanline < c.image_height) {
        const int v = static_cast<int>(c.next_scanline);
        for (int u = 0; u < width; ++u) {
            // Where stored (u, v) lands in the upright picture.
            int x = u;
            int y = v;
            switch (orientation) {
            case 2: x = width - 1 - u; break;
            case 3: x = width - 1 - u; y = height - 1 - v; break;
            case 4: y = height - 1 - v; break;
            case 5: x = v; y = u; break;
            case 6: x = height - 1 - v; y = u; break;
            case 7: x = height - 1 - v; y = width - 1 - u; break;
            case 8: x = v; y = width - 1 - u; break;
            default: break;
            }
            std::uint32_t p = 0;
            std::memcpy(&p, pixels + (static_cast<std::ptrdiff_t>(y) * stride) + (static_cast<std::ptrdiff_t>(x) * 4),
                        sizeof p);
            unsigned char* out = &row[static_cast<std::size_t>(u) * 3];
            out[0] = static_cast<unsigned char>(p >> 16U);
            out[1] = static_cast<unsigned char>(p >> 8U);
            out[2] = static_cast<unsigned char>(p);
        }
        JSAMPROW line = row.data();
        jpeg_write_scanlines(&c, &line, 1);
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    std::fclose(file);
    cairo_surface_destroy(png);
    return 0;
}
