#include "wallpaper.hpp"

#include "cairo_buffer.hpp"
#include "log.hpp"
#include "output.hpp"
#include "server.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <sys/eventfd.h>
#include <unistd.h>

namespace tetra {

namespace {

using Wallpaper_ = config::Desktop::Wallpaper;
using Fit = config::Desktop::Fit;
using config::Color;

struct Point {
    double x;
    double y;
};

Point lerp(Point a, Point b, double t) {
    return {a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t)};
}

using Triangle = std::array<Point, 3>;

Point centroid(const Triangle& t) {
    return {(t[0].x + t[1].x + t[2].x) / 3, (t[0].y + t[1].y + t[2].y) / 3};
}

// Shrunk towards its centre, leaving the gaps of the mark.
Triangle shrink(const Triangle& t, double amount) {
    const Point c = centroid(t);
    return {lerp(t[0], c, amount), lerp(t[1], c, amount), lerp(t[2], c, amount)};
}

void path(cairo_t* cr, const Triangle& t) {
    cairo_move_to(cr, t[0].x, t[0].y);
    cairo_line_to(cr, t[1].x, t[1].y);
    cairo_line_to(cr, t[2].x, t[2].y);
    cairo_close_path(cr);
}

Color mix(Color a, Color b, float t) {
    return {a.r + ((b.r - a.r) * t), a.g + ((b.g - a.g) * t), a.b + ((b.b - a.b) * t), 1};
}

void set_source(cairo_t* cr, Color c, double alpha = 1) {
    cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
}

double luminance(Color c) {
    return (0.2126 * c.r) + (0.7152 * c.g) + (0.0722 * c.b);
}

// The tetra mark as a paper net: a triangle split into four, the three
// corner faces filled (shaded as if lit from the top left), the middle face
// they fold up around outlined with dashed fold lines. The same geometry
// and gaps as the launcher button's mark (src/shell/bar.cpp).
void draw_logo(cairo_t* cr, int width, int height, Color accent, Color background) {
    constexpr Color kWhite{1, 1, 1, 1};
    constexpr Color kBlack{0, 0, 0, 1};
    constexpr double kGap = 0.12; // as the launcher's mark
    const double w = width;
    const double h = height;

    // About a quarter of the display's height, a little above the middle
    // (the bar is at the bottom).
    const double side = std::min(h * 0.34, w * 0.6);
    const double tall = side * 0.8660254; // equilateral height
    const Point center{w / 2, h * 0.47};
    const Point top{center.x, center.y - (tall * 0.58)};
    const Point left{center.x - (side / 2), center.y + (tall * 0.42)};
    const Point right{center.x + (side / 2), center.y + (tall * 0.42)};
    const Point mid_left = lerp(top, left, 0.5);
    const Point mid_right = lerp(top, right, 0.5);
    const Point mid_bottom = lerp(left, right, 0.5);
    const std::array<Triangle, 3> faces = {{
        shrink({top, mid_left, mid_right}, kGap),
        shrink({mid_left, left, mid_bottom}, kGap),
        shrink({mid_right, mid_bottom, right}, kGap),
    }};
    const Triangle middle = shrink({mid_left, mid_right, mid_bottom}, kGap);
    const bool light = luminance(background) > 0.5;
    const double line = std::max(1.0, side * 0.0045);

    // (No glow behind it: a gradient that faint bands into visible rings
    // in 8 bits per channel.)

    // A soft shadow under the faces: offset copies, fading.
    for (int i = 5; i >= 1; --i) {
        cairo_save(cr);
        cairo_translate(cr, 0, side * 0.0045 * i);
        for (const Triangle& face : faces) {
            path(cr, face);
        }
        set_source(cr, kBlack, light ? 0.025 : 0.05);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // The middle face: barely tinted, its edges the fold lines.
    path(cr, middle);
    set_source(cr, accent, 0.06);
    cairo_fill_preserve(cr);
    set_source(cr, accent, 0.6);
    cairo_set_line_width(cr, line);
    const std::array<double, 2> dashes{side * 0.022, side * 0.016};
    cairo_set_dash(cr, dashes.data(), static_cast<int>(dashes.size()), 0);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);

    // The faces, lit from the top left: the top one lightest, the right one
    // darkest; each with a gentle gradient and a light edge.
    const std::array<Color, 3> shades = {mix(accent, kWhite, 0.22F), accent, mix(accent, kBlack, 0.28F)};
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const Triangle& face = faces.at(i);
        const Color base = mix(shades.at(i), background, 0.12F); // a little quieter than the bar
        double min_y = face[0].y;
        double max_y = face[0].y;
        for (const Point& p : face) {
            min_y = std::min(min_y, p.y);
            max_y = std::max(max_y, p.y);
        }
        cairo_pattern_t* gradient = cairo_pattern_create_linear(0, min_y, 0, max_y);
        const Color upper = mix(base, kWhite, 0.10F);
        const Color lower = mix(base, kBlack, 0.08F);
        cairo_pattern_add_color_stop_rgb(gradient, 0, upper.r, upper.g, upper.b);
        cairo_pattern_add_color_stop_rgb(gradient, 1, lower.r, lower.g, lower.b);
        path(cr, face);
        cairo_set_source(cr, gradient);
        cairo_fill_preserve(cr);
        cairo_pattern_destroy(gradient);
        set_source(cr, kWhite, 0.12);
        cairo_set_line_width(cr, line * 0.8);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_stroke(cr);
    }
}

void draw_picture(cairo_t* cr, int width, int height, cairo_surface_t* image, Fit fit) {
    const double iw = cairo_image_surface_get_width(image);
    const double ih = cairo_image_surface_get_height(image);
    if (iw <= 0 || ih <= 0) {
        return;
    }
    double s = 1;
    switch (fit) {
    case Fit::Fill:
        s = std::max(width / iw, height / ih);
        break;
    case Fit::Fit:
        s = std::min(width / iw, height / ih);
        break;
    case Fit::Center:
        break; // one picture pixel per display pixel
    }
    cairo_save(cr);
    cairo_translate(cr, std::round((width - (iw * s)) / 2), std::round((height - (ih * s)) / 2));
    cairo_scale(cr, s, s);
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_rectangle(cr, 0, 0, iw, ih);
    cairo_fill(cr);
    cairo_restore(cr);
}

// Straight RGB(A) bytes to cairo's premultiplied native-endian ARGB32.
cairo_surface_t* to_cairo(GdkPixbuf* pixbuf, std::string& error) {
    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const int stride = gdk_pixbuf_get_rowstride(pixbuf);
    const bool alpha = gdk_pixbuf_get_has_alpha(pixbuf) != FALSE;
    if (gdk_pixbuf_get_bits_per_sample(pixbuf) != 8 || channels < 3) {
        error = "unsupported pixel format";
        return nullptr;
    }
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        error = "out of memory";
        return nullptr;
    }
    cairo_surface_flush(surface);
    const guchar* src = gdk_pixbuf_read_pixels(pixbuf);
    unsigned char* dst = cairo_image_surface_get_data(surface);
    const int dst_stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < height; ++y) {
        const guchar* in = src + (static_cast<std::ptrdiff_t>(y) * stride);
        unsigned char* out = dst + (static_cast<std::ptrdiff_t>(y) * dst_stride);
        for (int x = 0; x < width; ++x, in += channels, out += 4) {
            const std::uint32_t a = alpha ? in[3] : 255U;
            const auto premultiply = [a](std::uint32_t c) { return ((c * a) + 127U) / 255U; };
            const std::uint32_t pixel =
                (a << 24U) | (premultiply(in[0]) << 16U) | (premultiply(in[1]) << 8U) | premultiply(in[2]);
            std::memcpy(out, &pixel, sizeof pixel);
        }
    }
    cairo_surface_mark_dirty(surface);
    return surface;
}

} // namespace

Wallpaper::Wallpaper(Server& server) : server_(server) {
    desktop_ = server_.config().desktop;
    accent_ = server_.config().theme.accent;
    event_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (event_fd_ >= 0) {
        event_source_ = wl_event_loop_add_fd(
            wl_display_get_event_loop(server_.display), event_fd_, WL_EVENT_READABLE,
            [](int fd, uint32_t /*mask*/, void* data) {
                std::uint64_t count = 0;
                [[maybe_unused]] const ssize_t n = read(fd, &count, sizeof count);
                static_cast<Wallpaper*>(data)->take_result();
                return 0;
            },
            this);
    } else {
        log::error("wallpaper: eventfd failed; pictures cannot be loaded");
    }
    worker_ = std::jthread([this](const std::stop_token& stop) { worker_loop(stop); });
}

Wallpaper::~Wallpaper() {
    worker_ = {}; // stop and join: a decode in progress finishes first
    if (event_source_ != nullptr) {
        wl_event_source_remove(event_source_);
    }
    if (event_fd_ >= 0) {
        close(event_fd_);
    }
    if (result_ && result_->image != nullptr) {
        cairo_surface_destroy(result_->image);
    }
    drop_picture();
}

void Wallpaper::update() {
    const config::Config& config = server_.config();
    if (config.desktop == desktop_ && config.theme.accent == accent_) {
        return;
    }
    desktop_ = config.desktop;
    accent_ = config.theme.accent;
    const bool picture = desktop_.wallpaper == Wallpaper_::Picture && !desktop_.picture.empty();
    if (!picture || desktop_.picture != picture_path_ || desktop_.picture_fit != picture_fit_) {
        drop_picture(); // the outputs ask for the new one (ensure_covers)
    }
    ++generation_;
    redraw_outputs();
}

void Wallpaper::ensure_covers(int width, int height) {
    if (desktop_.wallpaper != Wallpaper_::Picture || desktop_.picture.empty() || event_source_ == nullptr) {
        return;
    }
    if (desktop_.picture == picture_path_ && width <= target_width_ && height <= target_height_) {
        return; // loaded (or loading) large enough
    }
    if (desktop_.picture != picture_path_) {
        target_width_ = 0;
        target_height_ = 0;
    }
    request(std::max(width, target_width_), std::max(height, target_height_));
}

void Wallpaper::request(int width, int height) {
    picture_path_ = desktop_.picture;
    picture_fit_ = desktop_.picture_fit;
    target_width_ = width;
    target_height_ = height;
    ++request_id_;
    {
        const std::lock_guard lock(mutex_);
        pending_ = Request{.id = request_id_, .path = picture_path_, .width = width, .height = height, .fit = picture_fit_};
    }
    wake_.notify_one();
}

void Wallpaper::take_result() {
    std::optional<Result> result;
    {
        const std::lock_guard lock(mutex_);
        result.swap(result_);
    }
    if (!result) {
        return;
    }
    if (result->id != request_id_) {
        if (result->image != nullptr) {
            cairo_surface_destroy(result->image); // superseded
        }
        return;
    }
    if (result->image == nullptr) {
        log::error("desktop picture {}: {}", picture_path_, result->error);
        return; // the colour stays; a new path or a larger display tries again
    }
    if (picture_ != nullptr) {
        cairo_surface_destroy(picture_);
    }
    picture_ = result->image;
    log::info("desktop picture {}: {}x{}", picture_path_, cairo_image_surface_get_width(picture_),
              cairo_image_surface_get_height(picture_));
    ++generation_;
    redraw_outputs();
}

void Wallpaper::drop_picture() {
    if (picture_ != nullptr) {
        cairo_surface_destroy(picture_);
        picture_ = nullptr;
    }
    picture_path_.clear();
    target_width_ = 0;
    target_height_ = 0;
    ++request_id_; // a load in progress is no longer wanted
}

void Wallpaper::redraw_outputs() {
    for (const auto& output : server_.outputs()) {
        output->update_background();
    }
}

wlr_buffer* Wallpaper::render(int width, int height, float scale) const {
    if (width <= 0 || height <= 0 || desktop_.wallpaper == Wallpaper_::Color) {
        return nullptr;
    }
    if (desktop_.wallpaper == Wallpaper_::Picture && picture_ == nullptr) {
        return nullptr;
    }
    const int pw = static_cast<int>(std::ceil(static_cast<float>(width) * scale));
    const int ph = static_cast<int>(std::ceil(static_cast<float>(height) * scale));
    CairoBuffer* buffer = CairoBuffer::create(pw, ph);
    cairo_t* cr = cairo_create(buffer->surface());
    set_source(cr, desktop_.background);
    cairo_paint(cr);
    if (desktop_.wallpaper == Wallpaper_::Logo) {
        draw_logo(cr, pw, ph, accent_, desktop_.background);
    } else {
        draw_picture(cr, pw, ph, picture_, desktop_.picture_fit);
    }
    cairo_destroy(cr);
    return buffer->base();
}

void Wallpaper::worker_loop(const std::stop_token& stop) {
    while (true) {
        Request request;
        {
            std::unique_lock lock(mutex_);
            if (!wake_.wait(lock, stop, [this] { return pending_.has_value(); })) {
                return; // stopping
            }
            request = std::move(*pending_);
            pending_.reset();
        }
        std::string error;
        cairo_surface_t* image = load(request, error);
        {
            const std::lock_guard lock(mutex_);
            if (result_ && result_->image != nullptr) {
                cairo_surface_destroy(result_->image); // never picked up: superseded
            }
            result_ = Result{.id = request.id, .image = image, .error = std::move(error)};
        }
        const std::uint64_t one = 1;
        [[maybe_unused]] const ssize_t n = write(event_fd_, &one, sizeof one);
    }
}

cairo_surface_t* Wallpaper::load(const Request& request, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(request.path, ec)) {
        error = "no such file";
        return nullptr;
    }
    int width = 0;
    int height = 0;
    if (gdk_pixbuf_get_file_info(request.path.c_str(), &width, &height) == nullptr || width <= 0 || height <= 0) {
        error = "not a picture gdk-pixbuf can read (or not readable)";
        return nullptr;
    }
    // Only as large as the largest display needs (never larger than it is).
    double s = 1;
    if (request.fit == Fit::Fill) {
        s = std::max(static_cast<double>(request.width) / width, static_cast<double>(request.height) / height);
    } else if (request.fit == Fit::Fit) {
        s = std::min(static_cast<double>(request.width) / width, static_cast<double>(request.height) / height);
    }
    s = std::min(s, 1.0);
    GError* gerror = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(
        request.path.c_str(), std::max(1, static_cast<int>(std::lround(width * s))),
        std::max(1, static_cast<int>(std::lround(height * s))), FALSE, &gerror);
    if (pixbuf == nullptr) {
        error = gerror != nullptr ? gerror->message : "cannot load";
        g_clear_error(&gerror);
        return nullptr;
    }
    GdkPixbuf* oriented = gdk_pixbuf_apply_embedded_orientation(pixbuf); // camera EXIF rotation
    g_object_unref(pixbuf);
    if (oriented == nullptr) {
        error = "cannot rotate";
        return nullptr;
    }
    cairo_surface_t* surface = to_cairo(oriented, error);
    g_object_unref(oriented);
    return surface;
}

} // namespace tetra
