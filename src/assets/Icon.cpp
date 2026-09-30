#include "yk/assets/Icon.hpp"
#include <cstdint>

namespace yk {
namespace {
std::uint32_t bigEndian(std::string_view bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value = (value << 8U) | static_cast<std::uint8_t>(bytes[at + i]);
    return value;
}
void putBigEndian(std::string &out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<char>((value >> static_cast<unsigned>(shift)) & 0xFFU));
}
} // namespace

Result<ImageSize> pngSize(std::string_view bytes) {
    static constexpr std::string_view signature("\x89PNG\r\n\x1a\n", 8);
    // Signature, then the IHDR chunk: length (13), "IHDR", width, height, ...
    if (bytes.size() < 24 || bytes.substr(0, 8) != signature || bytes.substr(12, 4) != "IHDR")
        return Error{"not a PNG picture"};
    const std::uint32_t width = bigEndian(bytes, 16), height = bigEndian(bytes, 20);
    if (width == 0 || height == 0 || width > 65536 || height > 65536)
        return Error{"the PNG header has an impossible size"};
    return ImageSize{static_cast<int>(width), static_cast<int>(height)};
}

Result<std::string> makeIcns(std::string_view png) {
    const auto size = pngSize(png);
    if (!size)
        return Error{"The app icon is " + size.error()};
    if (size.value().width != size.value().height)
        return Error{"The app icon must be square, this one is " +
                     std::to_string(size.value().width) + " x " +
                     std::to_string(size.value().height) + " pixels"};
    const int edge = size.value().width;
    if (edge < 128)
        return Error{"The app icon must be at least 128 x 128 pixels, this one is " +
                     std::to_string(edge)};
    // The icon types that carry a PNG: ic07 128, ic08 256, ic09 512, ic10 1024 pixels.
    const char *type = edge >= 1024 ? "ic10" : edge >= 512 ? "ic09" : edge >= 256 ? "ic08" : "ic07";
    std::string file = "icns";
    putBigEndian(file, static_cast<std::uint32_t>(8 + 8 + png.size()));
    file += type;
    putBigEndian(file, static_cast<std::uint32_t>(8 + png.size()));
    file.append(png);
    return file;
}
} // namespace yk
