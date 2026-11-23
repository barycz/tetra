#pragma once

#include <imgui.h>

#include <cstdint>
#include <span>

namespace tetra::ui {

// A GL texture from raw pixels, e.g. a notification's image-data. Create and
// destroy it while the toolkit's GL context is current (inside a draw
// callback, or anywhere on the UI thread after the first frame).
class Texture {
public:
    // `pixels`: rows of `stride` bytes, 3 (RGB) or 4 (RGBA, straight alpha)
    // bytes per pixel.
    Texture(int width, int height, int stride, int channels, std::span<const uint8_t> pixels);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture&&) = delete;

    [[nodiscard]] bool valid() const noexcept { return texture_ != 0; }
    [[nodiscard]] ImTextureRef ref() const { return ImTextureRef(static_cast<ImTextureID>(texture_)); }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    unsigned int texture_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace tetra::ui
