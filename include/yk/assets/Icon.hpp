#pragma once
#include "yk/core/Result.hpp"
#include <string>
#include <string_view>

// The app icon of an exported game. A project names one square PNG (Build settings); on macOS the
// exporter wraps it in the .icns file a bundle wants, so no image tool is needed on any system.
namespace yk {
struct ImageSize {
    int width{};
    int height{};
};
// The size of a PNG read from its header (nothing is decoded); an error when `bytes` is not a PNG.
Result<ImageSize> pngSize(std::string_view bytes);

// Wraps a square PNG of at least 128 pixels in an Apple icon file (.icns). The picture is stored
// as it is under the icon size it fits (128, 256, 512 or 1024 pixels): the system scales it for
// the Dock, Finder and app switcher. 1024 pixels gives the sharpest result.
Result<std::string> makeIcns(std::string_view png);

// Below this edge an app icon looks soft at Dock sizes on a Retina display.
inline constexpr int recommendedIconSize = 512;
} // namespace yk
