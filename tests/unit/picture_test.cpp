// Unit tests for the desktop picture decoder (src/compositor/picture.*):
// JPEG and PNG files written here, EXIF orientation, scaling, CMYK and
// files that are not pictures or are broken.

#include "picture.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <print>
#include <source_location>
#include <string>
#include <vector>

#include <jpeglib.h>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

int failures = 0;

void check(bool ok, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!ok) {
        ++failures;
        std::println(stderr, "FAIL picture_test.cpp line {}: {}", where.line(), what);
    }
}

struct Rgb {
    int r;
    int g;
    int b;
};

constexpr Rgb kRed{220, 30, 30};
constexpr Rgb kBlue{30, 30, 220};

// Left half red, right half blue.
Rgb halves(int x, int width) {
    return x < width / 2 ? kRed : kBlue;
}

// EXIF APP1 payload with only IFD0 Orientation, little-endian.
std::vector<unsigned char> exif(int orientation) {
    return {'E', 'x', 'i', 'f', 0, 0,                         // header
            'I', 'I', 42, 0, 8, 0, 0, 0,                      // TIFF, IFD0 at 8
            1, 0,                                             // one entry
            0x12, 0x01, 3, 0, 1, 0, 0, 0,                     // Orientation, SHORT, 1
            static_cast<unsigned char>(orientation), 0, 0, 0, // value
            0, 0, 0, 0};                                      // no next IFD
}

void write_jpeg(const fs::path& path, int width, int height, int orientation = 0, bool cmyk = false) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    jpeg_compress_struct c{};
    jpeg_error_mgr err{};
    c.err = jpeg_std_error(&err);
    jpeg_create_compress(&c);
    jpeg_stdio_dest(&c, file);
    c.image_width = static_cast<JDIMENSION>(width);
    c.image_height = static_cast<JDIMENSION>(height);
    c.input_components = cmyk ? 4 : 3;
    c.in_color_space = cmyk ? JCS_CMYK : JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, 95, TRUE);
    jpeg_start_compress(&c, TRUE);
    if (orientation != 0) {
        const std::vector<unsigned char> app1 = exif(orientation);
        jpeg_write_marker(&c, JPEG_APP0 + 1, app1.data(), static_cast<unsigned>(app1.size()));
    }
    std::vector<unsigned char> row(static_cast<std::size_t>(width) * c.input_components);
    while (c.next_scanline < c.image_height) {
        for (int x = 0; x < width; ++x) {
            unsigned char* p = &row[static_cast<std::size_t>(x) * c.input_components];
            if (cmyk) {
                std::memset(p, 255, 4); // Adobe-inverted: no ink at all
            } else {
                const Rgb rgb = halves(x, width);
                p[0] = static_cast<unsigned char>(rgb.r);
                p[1] = static_cast<unsigned char>(rgb.g);
                p[2] = static_cast<unsigned char>(rgb.b);
            }
        }
        JSAMPROW line = row.data();
        jpeg_write_scanlines(&c, &line, 1);
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    std::fclose(file);
}

void write_png(const fs::path& path, int width, int height) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, kRed.r / 255.0, kRed.g / 255.0, kRed.b / 255.0);
    cairo_rectangle(cr, 0, 0, width / 2.0, height);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, kBlue.r / 255.0, kBlue.g / 255.0, kBlue.b / 255.0);
    cairo_rectangle(cr, width / 2.0, 0, width / 2.0, height);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_write_to_png(s, path.c_str());
    cairo_surface_destroy(s);
}

Rgb pixel(cairo_surface_t* s, int x, int y) {
    cairo_surface_flush(s);
    const unsigned char* row = cairo_image_surface_get_data(s) + (static_cast<std::ptrdiff_t>(y) *
                                                                   cairo_image_surface_get_stride(s));
    std::uint32_t p = 0;
    std::memcpy(&p, row + (static_cast<std::ptrdiff_t>(x) * 4), sizeof p);
    return {static_cast<int>((p >> 16U) & 0xffU), static_cast<int>((p >> 8U) & 0xffU), static_cast<int>(p & 0xffU)};
}

bool near(Rgb a, Rgb b) {
    return std::abs(a.r - b.r) <= 24 && std::abs(a.g - b.g) <= 24 && std::abs(a.b - b.b) <= 24;
}

struct Loaded {
    cairo_surface_t* image = nullptr;
    std::string error;
    int width = 0;
    int height = 0;
    Loaded() = default;
    Loaded(const Loaded&) = delete;
    Loaded& operator=(const Loaded&) = delete;
    Loaded(Loaded&&) = delete;
    Loaded& operator=(Loaded&&) = delete;
    ~Loaded() {
        if (image != nullptr) {
            cairo_surface_destroy(image);
        }
    }
};

void load(Loaded& out, const fs::path& path, double scale = 1) {
    out.image = tetra::load_picture(path, [scale](int /*w*/, int /*h*/) { return scale; }, out.error);
    if (out.image != nullptr) {
        out.width = cairo_image_surface_get_width(out.image);
        out.height = cairo_image_surface_get_height(out.image);
    }
}

void pictures(const fs::path& dir) {
    write_png(dir / "halves.png", 40, 20);
    {
        Loaded p;
        load(p, dir / "halves.png");
        check(p.image != nullptr && p.width == 40 && p.height == 20, "png size");
        check(p.image != nullptr && near(pixel(p.image, 5, 10), kRed) && near(pixel(p.image, 35, 10), kBlue),
              "png colours");
    }
    {
        Loaded p;
        load(p, dir / "halves.png", 0.5);
        check(p.image != nullptr && p.width == 20 && p.height == 10, "png scaled down");
    }
    {
        Loaded p;
        load(p, dir / "halves.png", 3);
        check(p.image != nullptr && p.width == 40 && p.height == 20, "never enlarged");
    }

    write_jpeg(dir / "halves.jpg", 40, 20);
    {
        Loaded p;
        load(p, dir / "halves.jpg");
        check(p.image != nullptr && p.width == 40 && p.height == 20, "jpeg size");
        check(p.image != nullptr && near(pixel(p.image, 5, 10), kRed) && near(pixel(p.image, 35, 10), kBlue),
              "jpeg colours");
    }
    write_jpeg(dir / "turned.jpg", 40, 20, 6); // stored sideways: turn a quarter clockwise
    {
        Loaded p;
        load(p, dir / "turned.jpg");
        check(p.image != nullptr && p.width == 20 && p.height == 40, "orientation 6 swaps the size");
        check(p.image != nullptr && near(pixel(p.image, 10, 5), kRed) && near(pixel(p.image, 10, 35), kBlue),
              "orientation 6: the stored left half on top");
    }
    write_jpeg(dir / "upside-down.jpg", 40, 20, 3);
    {
        Loaded p;
        load(p, dir / "upside-down.jpg");
        check(p.image != nullptr && p.width == 40 && near(pixel(p.image, 5, 10), kBlue) &&
                  near(pixel(p.image, 35, 10), kRed),
              "orientation 3");
    }
    write_jpeg(dir / "anticlockwise.jpg", 40, 20, 8);
    {
        Loaded p;
        load(p, dir / "anticlockwise.jpg");
        check(p.image != nullptr && p.height == 40 && near(pixel(p.image, 10, 5), kBlue) &&
                  near(pixel(p.image, 10, 35), kRed),
              "orientation 8");
    }
    write_jpeg(dir / "large.jpg", 1600, 800);
    {
        Loaded p;
        load(p, dir / "large.jpg", 0.1); // decoded at 1/8, then scaled by cairo
        check(p.image != nullptr && p.width == 160 && p.height == 80, "jpeg scaled down");
        check(p.image != nullptr && near(pixel(p.image, 20, 40), kRed) && near(pixel(p.image, 140, 40), kBlue),
              "jpeg scaled colours");
    }
    write_jpeg(dir / "cmyk.jpg", 16, 16, 0, true);
    {
        Loaded p;
        load(p, dir / "cmyk.jpg");
        check(p.image != nullptr && near(pixel(p.image, 8, 8), {255, 255, 255}), "cmyk jpeg");
    }

    std::ofstream(dir / "notes.txt") << "not a picture\n";
    {
        Loaded p;
        load(p, dir / "notes.txt");
        check(p.image == nullptr && p.error == "not a JPEG or PNG picture", "text file: " + p.error);
    }
    {
        Loaded p;
        load(p, dir / "missing.jpg");
        check(p.image == nullptr && !p.error.empty(), "missing file");
    }
    {
        // A JPEG start followed by rubbish: libjpeg's error path (longjmp).
        std::ofstream broken(dir / "broken.jpg", std::ios::binary);
        broken << "\xff\xd8\xff\xe0" << std::string(64, 'x');
    }
    {
        Loaded p;
        load(p, dir / "broken.jpg");
        check(p.image == nullptr && !p.error.empty(), "broken jpeg: " + p.error);
    }
    {
        // A real JPEG cut in half: decodes what is there (grey below).
        fs::copy_file(dir / "large.jpg", dir / "truncated.jpg");
        fs::resize_file(dir / "truncated.jpg", fs::file_size(dir / "large.jpg") / 2);
    }
    {
        Loaded p;
        load(p, dir / "truncated.jpg");
        check(p.image != nullptr && p.width == 1600, "truncated jpeg: " + p.error);
    }
    {
        // A PNG signature followed by rubbish.
        std::ofstream(dir / "broken.png", std::ios::binary) << "\x89PNG\r\n\x1a\n" << std::string(64, 'x');
    }
    {
        Loaded p;
        load(p, dir / "broken.png");
        check(p.image == nullptr && !p.error.empty(), "broken png");
    }
}

} // namespace

int picture_tests() {
    std::string templ = (fs::temp_directory_path() / "tetra-picture-XXXXXX").string();
    if (mkdtemp(templ.data()) == nullptr) {
        std::println(stderr, "FAIL picture_test.cpp: mkdtemp");
        return 1;
    }
    const fs::path dir = templ;
    pictures(dir);
    fs::remove_all(dir);
    return failures;
}
