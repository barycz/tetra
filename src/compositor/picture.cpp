#include "picture.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

#include <jpeglib.h> // after <cstdio>: it uses FILE

#ifndef JCS_EXTENSIONS
#error "tetra needs libjpeg-turbo (the JCS_EXT_* output colour spaces)"
#endif

namespace tetra {

namespace {

using ScaleFor = std::function<double(int, int)>;

// A decoded picture as stored in the file: `image` may already be shrunk
// (JPEG DCT scaling); `width` x `height` is the full stored size.
struct Decoded {
    cairo_surface_t* image = nullptr;
    int orientation = 1; // EXIF: 1 upright ... 8; 5-8 swap width and height
    int width = 0;
    int height = 0;
};

using Message = std::array<char, JMSG_LENGTH_MAX>;

struct JpegError {
    jpeg_error_mgr mgr; // first: libjpeg hands back a pointer to it
    std::jmp_buf jump;
    Message message;
};

// The Orientation tag (0x0112) of IFD0 in an APP1 "Exif" segment; 1 when
// there is none or it is malformed.
int exif_orientation(jpeg_saved_marker_ptr markers) {
    for (jpeg_saved_marker_ptr m = markers; m != nullptr; m = m->next) {
        if (m->marker != JPEG_APP0 + 1 || m->data_length < 14 || std::memcmp(m->data, "Exif\0\0", 6) != 0) {
            continue;
        }
        const JOCTET* tiff = m->data + 6;
        const std::size_t size = m->data_length - 6;
        bool little = false;
        if (tiff[0] == 'I' && tiff[1] == 'I') {
            little = true;
        } else if (tiff[0] != 'M' || tiff[1] != 'M') {
            return 1;
        }
        const auto u16 = [&](std::size_t at) -> std::uint32_t {
            const std::uint32_t a = tiff[at];
            const std::uint32_t b = tiff[at + 1];
            return little ? (a | (b << 8U)) : ((a << 8U) | b);
        };
        const auto u32 = [&](std::size_t at) -> std::uint32_t {
            return little ? (u16(at) | (u16(at + 2) << 16U)) : ((u16(at) << 16U) | u16(at + 2));
        };
        if (u16(2) != 42) {
            return 1;
        }
        const std::size_t ifd = u32(4);
        if (ifd > size - 2) {
            return 1;
        }
        const std::uint32_t count = u16(ifd);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t entry = ifd + 2 + (static_cast<std::size_t>(i) * 12);
            if (entry + 12 > size) {
                return 1;
            }
            if (u16(entry) == 0x0112) {
                const std::uint32_t value = u16(entry + 8);
                return u16(entry + 2) == 3 && value >= 1 && value <= 8 ? static_cast<int>(value) : 1; // SHORT
            }
        }
        return 1;
    }
    return 1;
}

constexpr std::uint32_t xrgb(std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    return 0xff000000U | (r << 16U) | (g << 8U) | b;
}

// One row of CMYK samples to cairo RGB24. Adobe's files (the usual ones)
// store the inks inverted.
void cmyk_row(const unsigned char* in, unsigned char* out, unsigned width, bool inverted) {
    for (unsigned x = 0; x < width; ++x, in += 4, out += 4) {
        std::uint32_t c = in[0];
        std::uint32_t m = in[1];
        std::uint32_t y = in[2];
        std::uint32_t k = in[3];
        if (!inverted) {
            c = 255 - c;
            m = 255 - m;
            y = 255 - y;
            k = 255 - k;
        }
        const std::uint32_t pixel = xrgb((c * k) / 255, (m * k) / 255, (y * k) / 255);
        std::memcpy(out, &pixel, sizeof pixel);
    }
}

// libjpeg reports errors by longjmp(): nothing in here may need a destructor.
bool decode_jpeg(std::FILE* file, const ScaleFor& scale_for, Decoded& out, Message& message) {
    jpeg_decompress_struct cinfo{};
    JpegError error{};
    cinfo.err = jpeg_std_error(&error.mgr);
    error.mgr.error_exit = [](j_common_ptr common) {
        auto* e = reinterpret_cast<JpegError*>(common->err); // NOLINT: mgr is its first member
        (*common->err->format_message)(common, e->message.data());
        std::longjmp(e->jump, 1);
    };
    error.mgr.output_message = [](j_common_ptr /*common*/) {}; // warnings (corrupt data): it decodes anyway
    unsigned char* volatile row = nullptr;
    if (setjmp(error.jump) != 0) {
        jpeg_destroy_decompress(&cinfo);
        std::free(row);
        if (out.image != nullptr) {
            cairo_surface_destroy(out.image);
            out.image = nullptr;
        }
        message = error.message;
        return false;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, file);
    jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xffff);
    jpeg_read_header(&cinfo, TRUE);
    out.width = static_cast<int>(cinfo.image_width);
    out.height = static_cast<int>(cinfo.image_height);
    out.orientation = exif_orientation(cinfo.marker_list);
    const bool turned = out.orientation >= 5;
    const double s = std::min(1.0, scale_for(turned ? out.height : out.width, turned ? out.width : out.height));
    // The smallest n/8 that still covers what is needed; cairo does the rest.
    cinfo.scale_num = std::clamp(static_cast<unsigned>(std::ceil(s * 8)), 1U, 8U);
    cinfo.scale_denom = 8;
    const bool cmyk = cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK;
    // cairo RGB24 is a native-endian 0xXXRRGGBB word.
    constexpr J_COLOR_SPACE kXrgb = std::endian::native == std::endian::little ? JCS_EXT_BGRX : JCS_EXT_XRGB;
    cinfo.out_color_space = cmyk ? JCS_CMYK : kXrgb;
    jpeg_start_decompress(&cinfo);
    out.image = cairo_image_surface_create(CAIRO_FORMAT_RGB24, static_cast<int>(cinfo.output_width),
                                           static_cast<int>(cinfo.output_height));
    if (cairo_surface_status(out.image) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(out.image);
        out.image = nullptr;
        jpeg_destroy_decompress(&cinfo);
        std::snprintf(message.data(), message.size(), "too large (%ux%u)", cinfo.output_width, cinfo.output_height);
        return false;
    }
    cairo_surface_flush(out.image);
    unsigned char* data = cairo_image_surface_get_data(out.image);
    const auto stride = static_cast<std::size_t>(cairo_image_surface_get_stride(out.image));
    if (cmyk) {
        row = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(cinfo.output_width) * 4));
        if (row == nullptr) {
            std::longjmp(error.jump, 1); // cleans up; the message stays empty
        }
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char* line = data + (cinfo.output_scanline * stride);
        JSAMPROW target = cmyk ? row : line;
        jpeg_read_scanlines(&cinfo, &target, 1);
        if (cmyk) {
            cmyk_row(row, line, cinfo.output_width, cinfo.saw_Adobe_marker != FALSE);
        }
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    std::free(row);
    cairo_surface_mark_dirty(out.image);
    return true;
}

bool decode_png(std::FILE* file, Decoded& out, std::string& error) {
    out.image = cairo_image_surface_create_from_png_stream(
        [](void* closure, unsigned char* data, unsigned int length) {
            return std::fread(data, 1, length, static_cast<std::FILE*>(closure)) == length ? CAIRO_STATUS_SUCCESS
                                                                                             : CAIRO_STATUS_READ_ERROR;
        },
        file);
    if (cairo_surface_status(out.image) != CAIRO_STATUS_SUCCESS) {
        error = cairo_status_to_string(cairo_surface_status(out.image));
        cairo_surface_destroy(out.image);
        out.image = nullptr;
        return false;
    }
    out.width = cairo_image_surface_get_width(out.image);
    out.height = cairo_image_surface_get_height(out.image);
    return true;
}

// Where a point of the stored picture (as fractions of its width and
// height) lands in the upright one, per EXIF orientation: x = a*u + b*v + c.
struct Axis {
    double u;
    double v;
    double c;
};
struct Orientation {
    Axis x;
    Axis y;
};
constexpr std::array<Orientation, 8> kOrientations{{
    {{1, 0, 0}, {0, 1, 0}},   // 1 upright
    {{-1, 0, 1}, {0, 1, 0}},  // 2 mirrored
    {{-1, 0, 1}, {0, -1, 1}}, // 3 upside down
    {{1, 0, 0}, {0, -1, 1}},  // 4 mirrored upside down
    {{0, 1, 0}, {1, 0, 0}},   // 5 transposed
    {{0, -1, 1}, {1, 0, 0}},  // 6 turn a quarter clockwise
    {{0, -1, 1}, {-1, 0, 1}}, // 7 transversed
    {{0, 1, 0}, {-1, 0, 1}},  // 8 turn a quarter anticlockwise
}};

// Turns `d` upright and scales it to what scale_for asks (never enlarging).
cairo_surface_t* finish(const Decoded& d, const ScaleFor& scale_for, std::string& error) {
    const bool turned = d.orientation >= 5;
    const int upright_w = turned ? d.height : d.width;
    const int upright_h = turned ? d.width : d.height;
    const double s = std::min(1.0, scale_for(upright_w, upright_h));
    const int width = std::max(1, static_cast<int>(std::lround(upright_w * s)));
    const int height = std::max(1, static_cast<int>(std::lround(upright_h * s)));
    const int source_w = cairo_image_surface_get_width(d.image);
    const int source_h = cairo_image_surface_get_height(d.image);
    if (d.orientation == 1 && width == source_w && height == source_h) {
        return d.image;
    }
    cairo_surface_t* image = cairo_image_surface_create(cairo_image_surface_get_format(d.image), width, height);
    if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(image);
        cairo_surface_destroy(d.image);
        error = "out of memory";
        return nullptr;
    }
    const Orientation& o = kOrientations.at(static_cast<std::size_t>(d.orientation - 1));
    cairo_matrix_t m;
    cairo_matrix_init(&m, width * o.x.u / source_w, height * o.y.u / source_w, width * o.x.v / source_h,
                      height * o.y.v / source_h, width * o.x.c, height * o.y.c);
    cairo_t* cr = cairo_create(image);
    cairo_set_matrix(cr, &m);
    cairo_set_source_surface(cr, d.image, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD); // no darkened edges
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(d.image);
    return image;
}

} // namespace

cairo_surface_t* load_picture(const std::string& path, const ScaleFor& scale_for, std::string& error) {
    std::FILE* file = std::fopen(path.c_str(), "rbe"); // e: close-on-exec
    if (file == nullptr) {
        error = std::error_code(errno, std::generic_category()).message();
        return nullptr;
    }
    std::array<unsigned char, 8> magic{};
    const std::size_t n = std::fread(magic.data(), 1, magic.size(), file);
    std::rewind(file);
    Decoded decoded;
    bool ok = false;
    if (n >= 3 && magic[0] == 0xff && magic[1] == 0xd8 && magic[2] == 0xff) {
        Message message{};
        ok = decode_jpeg(file, scale_for, decoded, message);
        if (!ok) {
            error = message[0] != '\0' ? message.data() : "out of memory";
        }
    } else if (n == magic.size() && std::memcmp(magic.data(), "\x89PNG\r\n\x1a\n", magic.size()) == 0) {
        ok = decode_png(file, decoded, error);
    } else {
        error = "not a JPEG or PNG picture";
    }
    std::fclose(file);
    return ok ? finish(decoded, scale_for, error) : nullptr;
}

} // namespace tetra
