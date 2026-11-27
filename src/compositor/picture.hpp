#pragma once

#include <cairo.h>

#include <functional>
#include <string>

namespace tetra {

// Decodes a JPEG (libjpeg-turbo) or PNG (cairo) file into a cairo image
// surface, upright (a JPEG's EXIF orientation applied) and scaled by
// `scale_for(upright width, upright height)`, never enlarged. A JPEG much
// larger than needed is decoded at 1/2, 1/4 or 1/8 of its size directly.
// Returns null, with the reason in `error`, when the file cannot be read or
// is neither format. The caller owns the surface.
[[nodiscard]] cairo_surface_t* load_picture(const std::string& path,
                                            const std::function<double(int width, int height)>& scale_for,
                                            std::string& error);

} // namespace tetra
