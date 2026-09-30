#pragma once
#include "yk/core/Json.hpp"
#include "yk/core/Result.hpp"
#include <array>
#include <optional>
#include <string>

namespace yk {
enum class TextureFilter { Nearest, Linear };

// Import settings a texture may carry in a sidecar file next to it: "assets/tiles/stone.png" is
// described by "assets/tiles/stone.png.ykmeta". Every field is optional and falls back to the
// project's TextureDefaults, so most textures need no sidecar at all.
//
//   {"format":"yk.texture","version":1,"pixelsPerUnit":64,"filter":"linear",
//    "columns":8,"rows":4,"border":[left, top, right, bottom]}
//
// `pixelsPerUnit` says how many texture pixels make one world unit (so tiles and slice borders have
// a world size); `columns`/`rows` are the default sprite-sheet grid; `border` (in pixels) is the
// nine-slice frame of a Sliced sprite: the corners keep their size, the rest tiles or stretches.
struct TextureMeta {
    std::optional<float> pixelsPerUnit;
    std::optional<TextureFilter> filter;
    std::optional<int> columns, rows;
    std::optional<std::array<int, 4>> border; // Left, top, right, bottom, in pixels.

    static constexpr const char *extension = ".ykmeta";
    static constexpr const char *formatName = "yk.texture";
    // "assets/a.png" -> "assets/a.png.ykmeta".
    static std::string sidecarPath(const std::string &texturePath) {
        return texturePath + extension;
    }
    bool empty() const {
        return !pixelsPerUnit && !filter && !columns && !rows && !border;
    }
    Json toJson() const;
    static Result<TextureMeta> fromJson(const Json &json);
};

// Project-wide fallbacks for textures without (or with partial) metadata.
struct TextureDefaults {
    float pixelsPerUnit{64.0F};
    TextureFilter filter{TextureFilter::Linear};
    Json toJson() const;
    static Result<TextureDefaults> fromJson(const Json &json);
};

// What the renderer uses for one texture once defaults and metadata are merged.
struct ResolvedTexture {
    float pixelsPerUnit{64.0F};
    TextureFilter filter{TextureFilter::Linear};
    int columns{1}, rows{1};
    std::array<int, 4> border{0, 0, 0, 0};
    bool hasBorder() const {
        return border[0] + border[1] + border[2] + border[3] > 0;
    }
};
ResolvedTexture resolve(const TextureDefaults &defaults, const TextureMeta &meta);
} // namespace yk
