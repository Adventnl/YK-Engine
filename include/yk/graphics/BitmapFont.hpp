#pragma once
#include "yk/graphics/Renderer.hpp"
#include <memory>
#include <string_view>

namespace yk {
// A built-in 5x7 pixel font for HUD text, debug overlays and placeholder UI. It needs no font file,
// so an unfinished project can still show readable text; final UI art replaces it later.
// Printable ASCII only (other bytes draw '?'). '\n' starts a new line.
class BitmapFont {
  public:
    static constexpr int glyphWidth = 5;
    static constexpr int glyphHeight = 7;
    static constexpr int advance = 6;     // Pixels per character at scale 1.
    static constexpr int lineAdvance = 9; // Pixels per line at scale 1.

    static Result<std::unique_ptr<BitmapFont>> create(Renderer &renderer);
    // Pixel size of the text at `scale` (a whole-number multiplier looks best).
    static Vec2 measure(std::string_view text, float scale);
    // Submits one sprite per glyph with its top-left at `topLeft` (units of the current pass).
    Status draw(Renderer &renderer, std::string_view text, Vec2 topLeft, float scale, Color color,
                int layer = 0, float depth = 0.0F) const;

  private:
    BitmapFont() = default;
    TextureHandle atlas_;
};
} // namespace yk
