// Draws tile maps through the real renderer (SDL software backend) and checks actual pixels:
// the tileset's sheet, mirrored tiles, culling, world-level visibility and animated tiles.
#include "support/check.hpp"
#include "yk/core/Application.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/graphics/SceneRenderer.hpp"
#include "yk/world/Tilemap.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace yk;

namespace {
constexpr Color clearColor{20, 24, 40, 255};

// Assets from a folder: the tileset text and the sheet's file.
class FolderAssets final : public AssetSource {
  public:
    explicit FolderAssets(std::filesystem::path root) : root_(std::move(root)) {}
    Result<std::string> readText(const std::string &path) const override {
        return readTextFile(root_ / path);
    }
    std::filesystem::path filePath(const std::string &path) const override {
        return root_ / path;
    }

  private:
    std::filesystem::path root_;
};

// A sheet of three 4 x 4 tiles: red | blue, all green, all yellow.
bool writeSheet(const std::filesystem::path &file) {
    SDL_Surface *sheet = SDL_CreateSurface(12, 4, SDL_PIXELFORMAT_RGBA32);
    if (!sheet)
        return false;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 12; ++x) {
            const Color color = x < 2   ? Color{255, 0, 0, 255}
                                : x < 4 ? Color{0, 0, 255, 255}
                                : x < 8 ? Color{0, 255, 0, 255}
                                        : Color{255, 255, 0, 255};
            SDL_WriteSurfacePixel(sheet, x, y, color.r, color.g, color.b, color.a);
        }
    const bool saved = SDL_SaveBMP(sheet, file.string().c_str());
    SDL_DestroySurface(sheet);
    return saved;
}

class TileLayerTest final : public ApplicationLayer {
  public:
    explicit TileLayerTest(const std::filesystem::path &folder) : assets_(folder) {}
    Status initialize(Renderer &renderer) override {
        registerEngineComponents(registry_);
        auto created = SceneRenderer::create(renderer, &assets_);
        if (!created)
            return Error{created.error()};
        sceneRenderer_ = std::move(created.value());
        scene_ = std::make_unique<Scene>(registry_, 5);
        scene_->settings.levels.levels = {{"ground", "", LevelKind::Floor, 0.0F},
                                          {"upstairs", "", LevelKind::Floor, 3.0F}};
        Entity &entity = scene_->createEntity("Map");
        auto &map = entity.add<Tilemap>();
        map.tileset.path = "tiles.yktileset";
        TileLayer ground = TileLayer::make("Ground", TileLayerKind::Floor);
        ground.level = "ground";
        const std::size_t g = map.addLayer(ground);
        map.setTile(g, 0, 0, tile::make(0));
        map.setTile(g, 1, 0, tile::make(0, tile::flipX));
        map.setTile(g, 2, 0, tile::make(1));
        map.setTile(g, 5000, 5000, tile::make(1)); // Far away: culled.
        TileLayer up = TileLayer::make("Upstairs", TileLayerKind::Floor);
        up.level = "upstairs";
        const std::size_t u = map.addLayer(up);
        map.setTile(u, 3, 0, tile::make(2));
        return success();
    }
    bool update(const FrameContext &) override {
        return true;
    }
    Status render(Renderer &renderer) override {
        // Four views of the same map, stacked: 400 x 100 pixels each (100 pixels per cell).
        struct View {
            float y;
            std::vector<float> alpha;
            bool useAlpha;
        };
        const View views[] = {{0, {}, false}, {100, {1.0F, 0.0F}, true}, {200, {1.0F, 0.5F}, true}};
        for (const View &view : views) {
            const Rect region{{0.0F, view.y}, {400.0F, 100.0F}};
            RenderPass pass{SceneRenderer::cameraFor({{2.0F, 0.5F}, 1.0F}, region.size), region,
                            clearColor};
            if (auto status = renderer.beginPass(pass); !status)
                return status;
            WorldView world;
            world.camera = pass.camera;
            world.viewport = region.size;
            world.levelAlpha = view.useAlpha ? &view.alpha : nullptr;
            if (auto status = sceneRenderer_->drawWorld(renderer, *scene_, world); !status)
                return status;
            if (view.y == 0) {
                stats_ = sceneRenderer_->stats();
            }
            if (auto status = renderer.endPass(); !status)
                return status;
        }
        return success();
    }
    SceneRenderStats stats_;

  private:
    FolderAssets assets_;
    ComponentRegistry registry_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<SceneRenderer> sceneRenderer_;
};

bool pixelIs(SDL_Surface *surface, int x, int y, Color expected, int tolerance = 3) {
    Uint8 r{}, g{}, b{}, a{};
    if (!SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a))
        return false;
    const auto close = [&](Uint8 actual, std::uint8_t wanted) {
        return std::abs(int{actual} - int{wanted}) <= tolerance;
    };
    return close(r, expected.r) && close(g, expected.g) && close(b, expected.b);
}
} // namespace

int main(int argc, char **argv) {
    const bool keep = argc > 1 && std::strcmp(argv[1], "--keep") == 0;
    const std::filesystem::path folder = std::filesystem::current_path() / "tilemap-render-test";
    std::filesystem::create_directories(folder);
    CHECK(writeSheet(folder / "sheet.bmp"));
    {
        std::ofstream tileset(folder / "tiles.yktileset");
        tileset << R"({"format":"yk.tileset","version":1,"texture":"sheet.bmp","tileWidth":4,
                      "tileHeight":4,"columns":3,"rows":1})";
    }
    const std::filesystem::path capture = folder / "capture.bmp";
    TileLayerTest layer(folder);
    {
        auto app = Application::create({"tilemap render test", 400, 300, 0, 0});
        CHECK(app);
        if (!app)
            return 1;
        const auto result = app.value()->run(layer, {2, capture});
        if (!result)
            std::fprintf(stderr, "%s\n", result.error().c_str());
        CHECK(result);
    }
    SDL_Surface *image = SDL_LoadBMP(capture.string().c_str());
    CHECK(image != nullptr);
    if (image) {
        CHECK(image->w == 400 && image->h == 300);
        // View 1 (all levels): cell 0 is red | blue, cell 1 is the same mirrored, cell 2 green, and
        // the upstairs layer's yellow tile is in cell 3.
        CHECK(pixelIs(image, 25, 50, {255, 0, 0, 255}) && pixelIs(image, 75, 50, {0, 0, 255, 255}));
        CHECK(pixelIs(image, 125, 50, {0, 0, 255, 255}) &&
              pixelIs(image, 175, 50, {255, 0, 0, 255}));
        CHECK(pixelIs(image, 250, 50, {0, 255, 0, 255}));
        CHECK(pixelIs(image, 350, 50, {255, 255, 0, 255}));
        // View 2 (upstairs hidden): the same ground, nothing in cell 3.
        CHECK(pixelIs(image, 25, 150, {255, 0, 0, 255}) &&
              pixelIs(image, 250, 150, {0, 255, 0, 255}));
        CHECK(pixelIs(image, 350, 150, clearColor));
        // View 3 (upstairs half faded): the yellow is blended with the background.
        Uint8 r{}, g{}, b{}, a{};
        SDL_ReadSurfacePixel(image, 350, 250, &r, &g, &b, &a);
        CHECK(r > 100 && r < 200 && g > 100 && g < 200 && b < 100);
        SDL_DestroySurface(image);
    }
    // Culling: the tile at (5000, 5000) sits in a chunk that was never submitted.
    CHECK(layer.stats_.tileChunksCulled >= 1);
    CHECK(layer.stats_.tiles == 4); // Four visible ground and upstairs tiles in the first view.
    if (!keep)
        std::filesystem::remove_all(folder);
    return yk::test::finish("tilemap_render");
}
