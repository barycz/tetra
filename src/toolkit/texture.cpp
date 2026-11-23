#include "texture.hpp"

#include <GLES3/gl3.h>

#include <vector>

namespace tetra::ui {

Texture::Texture(int width, int height, int stride, int channels, std::span<const uint8_t> pixels)
    : width_(width), height_(height) {
    if (width <= 0 || height <= 0 || (channels != 3 && channels != 4) || stride < width * channels ||
        pixels.size() < static_cast<std::size_t>(stride) * static_cast<std::size_t>(height - 1) +
                            static_cast<std::size_t>(width * channels)) {
        return;
    }
    // Repack to tight RGBA rows.
    std::vector<uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto src = (static_cast<std::size_t>(y) * stride) + (static_cast<std::size_t>(x) * channels);
            const auto dst = (static_cast<std::size_t>(y) * width + x) * 4;
            rgba[dst + 0] = pixels[src + 0];
            rgba[dst + 1] = pixels[src + 1];
            rgba[dst + 2] = pixels[src + 2];
            rgba[dst + 3] = channels == 4 ? pixels[src + 3] : 255;
        }
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    texture_ = texture;
}

Texture::~Texture() {
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
    }
}

} // namespace tetra::ui
