// Tiled and nine-slice sprites, sheet frames, additive blending, parallax and culling, checked on
// real pixels through the SDL software renderer. Textures are tiny BMP files written at start-up,
// with .ykmeta sidecars, so the whole asset path (files, metadata, cache, filter) is exercised.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/core/Application.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/graphics/SceneRenderer.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#include <filesystem>

using namespace yk;

namespace {
constexpr int regionWidth = 200, regionHeight = 100;
constexpr float pixelsPerUnitOnScreen = 20.0F; // Region shows 10 x 5 world units.

struct Swatch {
    int x, y, w, h;
    Color color;
};
// Writes a BMP made of colored rectangles over an opaque background.
bool writeBmp(const std::filesystem::path &path, int width, int height, Color background,
              const std::vector<Swatch> &swatches) {
    SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return false;
    SDL_FillSurfaceRect(surface, nullptr,
                        SDL_MapSurfaceRGBA(surface, background.r, background.g, background.b, 255));
    for (const Swatch &swatch : swatches) {
        const SDL_Rect rect{swatch.x, swatch.y, swatch.w, swatch.h};
        SDL_FillSurfaceRect(surface, &rect,
                            SDL_MapSurfaceRGBA(surface, swatch.color.r, swatch.color.g,
                                               swatch.color.b, 255));
    }
    const bool saved = SDL_SaveBMP(surface, path.string().c_str());
    SDL_DestroySurface(surface);
    return saved;
}

constexpr Color red{255, 0, 0, 255}, green{0, 255, 0, 255}, blue{0, 0, 255, 255},
    yellow{255, 255, 0, 255};
constexpr Color clearGray{40, 40, 40, 255}, additiveResult{200, 150, 100, 255},
    missingMagenta{255, 0, 220, 255};

struct Case {
    std::unique_ptr<Scene> scene;
    Vec2 center{};
    Rect region;
    Color clear{clearGray};
    bool parallax{false};
    SceneRenderStats stats;
};

class ModesLayer final : public ApplicationLayer {
  public:
    ModesLayer(const Project &project, const ProjectAssets &assets)
        : project_(project), assets_(assets) {}

    Status initialize(Renderer &renderer) override {
        registerEngineComponents(registry_);
        auto created = SceneRenderer::create(renderer, &assets_, project_.textures);
        if (!created)
            return Error{created.error()};
        sceneRenderer_ = std::move(created.value());
        int slot = 0;
        const auto add = [&](Vec2 center) -> Case & {
            cases_.emplace_back();
            Case &made = cases_.back();
            made.scene = std::make_unique<Scene>(registry_, 100 + static_cast<std::uint64_t>(slot));
            made.center = center;
            made.region = {{static_cast<float>((slot % 4) * regionWidth),
                            static_cast<float>((slot / 4) * regionHeight)},
                           {regionWidth, regionHeight}};
            ++slot;
            return made;
        };
        const auto sprite = [&](Case &c, const char *name, Vec2 at, Vec2 size,
                                const char *texture) -> SpriteRenderer & {
            Entity &entity = c.scene->createEntity(name);
            entity.transform().position = at;
            auto &component = entity.add<SpriteRenderer>();
            component.size = size;
            component.texture.path = texture;
            return component;
        };

        // 0: tiled. Tile texture is 2x2 px at 2 px per unit: one tile is 1 x 1 unit. The sprite is
        // 3.5 x 2 units, so the last column is half a tile (cropped, not squashed).
        {
            Case &c = add({0, 0});
            auto &s = sprite(c, "Tiled", {0, 0}, {3.5F, 2.0F}, "assets/tile.bmp");
            s.drawMode = SpriteDrawMode::Tiled;
        }
        // 1: nine-slice, 6x6 px with a 2 px border at 2 px per unit: corners are 1 x 1 unit.
        {
            Case &c = add({0, 0});
            auto &s = sprite(c, "Sliced", {0, 0}, {5.0F, 3.0F}, "assets/frame.bmp");
            s.drawMode = SpriteDrawMode::Sliced;
        }
        // 2: sheet frames from metadata (2 columns) and an explicit frame, plus a flipped copy.
        {
            Case &c = add({0, 0});
            auto &left = sprite(c, "Frame0", {-2, 0}, {2, 2}, "assets/sheet.bmp");
            left.frame = 0;
            auto &right = sprite(c, "Frame1", {0.5F, 0}, {2, 2}, "assets/sheet.bmp");
            right.frame = 1;
            auto &flipped = sprite(c, "Flipped", {3.0F, 0}, {2, 2}, "assets/sheet.bmp");
            flipped.frame = 0;
            flipped.flipX = true;
        }
        // 3: additive blending over a mid-gray clear.
        {
            Case &c = add({0, 0});
            c.clear = {100, 100, 100, 255};
            auto &s = sprite(c, "Glow", {0, 0}, {2, 2}, "");
            s.color = {100, 50, 0, 255};
            s.blend = SpriteBlend::Additive;
        }
        // 4 and 5: parallax. The same scene seen from two camera positions.
        for (const float cameraX : {0.0F, 6.0F}) {
            Case &c = add({cameraX, 0});
            c.parallax = true;
            auto &world = sprite(c, "World", {-2, 0}, {1, 1}, "");
            world.color = red; // Moves with the world.
            auto &far = sprite(c, "Far", {2, 0}, {1, 1}, "");
            far.color = green;
            far.parallax = {0.0F, 0.0F}; // Fixed on screen.
        }
        // 6: culling. A far-away sprite and a huge tiled background.
        {
            Case &c = add({0, 0});
            auto &away = sprite(c, "Away", {500, 0}, {1, 1}, "");
            away.color = red;
            auto &background = sprite(c, "Background", {0, 0}, {2000, 2000}, "assets/tile.bmp");
            background.drawMode = SpriteDrawMode::Tiled;
        }
        // 7: a texture that does not exist becomes a magenta placeholder.
        {
            Case &c = add({0, 0});
            sprite(c, "Missing", {0, 0}, {2, 2}, "assets/does-not-exist.bmp");
        }
        return success();
    }
    bool update(const FrameContext &) override {
        return true;
    }
    Status render(Renderer &renderer) override {
        for (Case &c : cases_) {
            const Camera2D camera =
                SceneRenderer::cameraFor({c.center, 5.0F}, c.region.size);
            RenderPass pass{camera, c.region, c.clear};
            if (auto begun = renderer.beginPass(pass); !begun)
                return begun;
            WorldView view{camera, c.region.size, c.parallax};
            auto drawn = sceneRenderer_->drawWorld(renderer, *c.scene, view);
            c.stats = sceneRenderer_->stats();
            if (auto ended = renderer.endPass(); !ended)
                return ended;
            if (!drawn)
                return drawn;
        }
        return success();
    }
    const std::vector<Case> &cases() const {
        return cases_;
    }

  private:
    const Project &project_;
    const ProjectAssets &assets_;
    ComponentRegistry registry_;
    std::unique_ptr<SceneRenderer> sceneRenderer_;
    std::vector<Case> cases_;
};

// Screen pixel of world point (x, y) in case `index` (each case is centered on `center`).
struct Probe {
    SDL_Surface *image;
    const Case &c;
    bool at(float x, float y, Color expected, int tolerance = 3) const {
        const int px = static_cast<int>(c.region.position.x + regionWidth / 2.0F +
                                        (x - c.center.x) * pixelsPerUnitOnScreen);
        const int py = static_cast<int>(c.region.position.y + regionHeight / 2.0F +
                                        (y - c.center.y) * pixelsPerUnitOnScreen);
        Uint8 r{}, g{}, b{}, a{};
        if (!SDL_ReadSurfacePixel(image, px, py, &r, &g, &b, &a))
            return false;
        const auto close = [&](Uint8 actual, std::uint8_t wanted) {
            return std::abs(int{actual} - int{wanted}) <= tolerance;
        };
        const bool ok = close(r, expected.r) && close(g, expected.g) && close(b, expected.b);
        if (!ok)
            std::fprintf(stderr, "  pixel at world (%g, %g) = (%d,%d,%d), wanted (%d,%d,%d)\n",
                         static_cast<double>(x), static_cast<double>(y), r, g, b, expected.r,
                         expected.g, expected.b);
        return ok;
    }
};
} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::current_path() / "render-modes-project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets");
    // Tile: 2x2 px, one color per pixel, so a tile is a 2 x 2 checker of four colors.
    CHECK(writeBmp(root / "assets/tile.bmp", 2, 2, red,
                   {{1, 0, 1, 1, green}, {0, 1, 1, 1, blue}, {1, 1, 1, 1, yellow}}));
    // Nine-slice: red corners, green edges, blue middle (6x6 px, 2 px border).
    CHECK(writeBmp(root / "assets/frame.bmp", 6, 6, green,
                   {{0, 0, 2, 2, red}, {4, 0, 2, 2, red}, {0, 4, 2, 2, red}, {4, 4, 2, 2, red},
                    {2, 2, 2, 2, blue}}));
    // Sheet: two 2x2 cells, the left red over blue, the right green over yellow.
    CHECK(writeBmp(root / "assets/sheet.bmp", 4, 2, red,
                   {{0, 1, 2, 1, blue}, {2, 0, 2, 1, green}, {2, 1, 2, 1, yellow}}));
    const auto sidecar = [&](const char *name, const char *text) {
        CHECK(writeTextFileAtomic(root / "assets" / name, text));
    };
    sidecar("tile.bmp.ykmeta", R"({"format":"yk.texture","version":1,"pixelsPerUnit":2,"filter":"nearest"})");
    sidecar("frame.bmp.ykmeta", R"({"format":"yk.texture","version":1,"pixelsPerUnit":2,"filter":"nearest","border":[2,2,2,2]})");
    sidecar("sheet.bmp.ykmeta", R"({"format":"yk.texture","version":1,"pixelsPerUnit":2,"filter":"nearest","columns":2,"rows":1})");

    Project project = Project::create(root, "Render Modes");
    ProjectAssets assets(project);
    const std::filesystem::path capture = root / "modes.bmp";
    std::vector<Case> cases;
    {
        auto app = Application::create({"render modes test", 4 * regionWidth, 2 * regionHeight, 0, 0});
        CHECK(app);
        if (!app)
            return 1;
        ModesLayer layer(project, assets);
        const auto result = app.value()->run(layer, {2, capture});
        if (!result)
            std::fprintf(stderr, "%s\n", result.error().c_str());
        CHECK(result);
        cases.reserve(layer.cases().size());
        for (const Case &c : layer.cases()) {
            cases.emplace_back();
            cases.back().center = c.center;
            cases.back().region = c.region;
            cases.back().stats = c.stats;
        }
    }
    SDL_Surface *image = SDL_LoadBMP(capture.string().c_str());
    CHECK(image != nullptr);
    if (!image)
        return yk::test::finish("render_modes");
    CHECK(cases.size() == 8);
    if (cases.size() != 8)
        return yk::test::finish("render_modes");

    // 0: Tiled. Sprite spans x in [-1.75, 1.75], y in [-1, 1]; tiles start at its top-left, so tile
    // (column, row) covers x = -1.75 + column, y = -1 + row. Each tile is red|green over blue|yellow
    // (each of those quarter-tiles is 0.5 units).
    {
        const Probe probe{image, cases[0]};
        CHECK(probe.at(-1.5F, -0.75F, red));    // Tile 0, top left quarter.
        CHECK(probe.at(-1.0F, -0.75F, green));  // Tile 0, top right quarter.
        CHECK(probe.at(-1.5F, -0.25F, blue));
        CHECK(probe.at(-1.0F, -0.25F, yellow));
        CHECK(probe.at(-0.5F, -0.75F, red));    // Tile 1 repeats it.
        CHECK(probe.at(-0.5F, 0.25F, red));     // Row 1.
        CHECK(probe.at(1.0F, 0.75F, yellow));   // Tile 2, bottom right quarter.
        // The last column is half a tile: only its left half (red over blue) appears, at natural
        // size, not squashed.
        CHECK(probe.at(1.5F, -0.75F, red));
        CHECK(probe.at(1.5F, -0.25F, blue));
        // Outside the sprite the clear color shows.
        CHECK(probe.at(2.2F, 0.0F, clearGray));
        CHECK(cases[0].stats.quads == 8); // 4 columns x 2 rows.
    }
    // 1: Sliced, 5 x 3 units: 1 x 1 corners, edges 3 x 1 (top/bottom) and 1 x 1 (left/right).
    {
        const Probe probe{image, cases[1]};
        CHECK(probe.at(-2.25F, -1.25F, red));   // Corners.
        CHECK(probe.at(2.25F, -1.25F, red));
        CHECK(probe.at(-2.25F, 1.25F, red));
        CHECK(probe.at(2.25F, 1.25F, red));
        CHECK(probe.at(0.0F, -1.25F, green));   // Top edge.
        CHECK(probe.at(0.0F, 1.25F, green));    // Bottom edge.
        CHECK(probe.at(-2.25F, 0.0F, green));   // Left edge.
        CHECK(probe.at(0.0F, 0.0F, blue));      // Middle.
        CHECK(probe.at(1.0F, 0.4F, blue));
        // The corners keep their size: a point 1.4 units in from the corner is not red any more.
        CHECK(probe.at(-1.1F, -1.25F, green));
    }
    // 2: Sheet frames come from the texture's metadata; flipX mirrors within the frame.
    {
        const Probe probe{image, cases[2]};
        CHECK(probe.at(-2.5F, -0.5F, red));     // Frame 0, top row red...
        CHECK(probe.at(-2.5F, 0.5F, blue));     // ...bottom row blue.
        CHECK(probe.at(0.0F, -0.5F, green));    // Frame 1: green over yellow.
        CHECK(probe.at(0.0F, 0.5F, yellow));
        CHECK(probe.at(3.0F, -0.5F, red));      // Frame 0 mirrored: uniform rows stay as they are.
        CHECK(probe.at(3.0F, 0.5F, blue));
    }
    // 3: Additive: 100 + (100, 50, 0) per channel.
    const Probe additive{image, cases[3]};
    CHECK(additive.at(0.0F, 0.0F, additiveResult));
    // 4 and 5: parallax. "World" moves with the camera, "Far" stays on the same pixel.
    {
        const Probe near{image, cases[4]}, moved{image, cases[5]};
        CHECK(near.at(-2.0F, 0.0F, red));    // At its world position with the camera at 0.
        CHECK(near.at(2.0F, 0.0F, green));
        // The camera moved 6 units right. The world sprite (x = -2) is 8 units left of the camera,
        // outside the 10 unit wide view. The fixed one (parallax 0) is still 2 units right of the
        // camera's center, exactly where it was on screen before.
        CHECK(moved.at(6.0F + 2.0F, 0.0F, green));
        CHECK(moved.at(6.0F - 2.0F, 0.0F, clearGray)); // Nothing where "World" was.
    }
    // 6: culling. The far sprite is skipped; the 2000 x 2000 tiled background submits only what
    // shows (a 10 x 5 unit view is at most 12 x 7 tiles) rather than four million quads.
    CHECK(cases[6].stats.culled >= 1);
    CHECK(cases[6].stats.quads > 0 && cases[6].stats.quads <= 12 * 7);
    // Tile boundaries fall on whole units, so (0.25, 0.25) is the red quarter of some tile.
    const Probe background{image, cases[6]};
    CHECK(background.at(0.25F, 0.25F, red));
    // 7: the missing texture is a magenta square (255, 0, 220).
    const Probe missing{image, cases[7]};
    CHECK(missing.at(0.0F, 0.0F, missingMagenta));
    SDL_DestroySurface(image);
    std::filesystem::remove_all(root);
    return yk::test::finish("render_modes");
}
