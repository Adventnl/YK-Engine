// Tilesets and tile maps: the data, the files, the component, and what solid tiles do in physics.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include "yk/world/Tilemap.hpp"
#include "yk/world/Tileset.hpp"
#include <algorithm>
#include <cmath>

using namespace yk;

namespace {
const char *tilesetText = R"({
  "format": "yk.tileset", "version": 1, "name": "Test",
  "texture": "assets/sheet.png", "tileWidth": 16, "tileHeight": 16, "columns": 4, "rows": 2,
  "tiles": [
    {"id": 0, "tags": ["floor"]},
    {"id": 1, "solid": true, "opaque": true, "noiseDamping": 0.8, "tags": ["wall"],
     "modify": {"action": "Dig", "resistance": 40, "result": 0, "material": "dirt"}},
    {"id": 2, "solid": true, "collider": [0.4, 0.0, 0.2, 1.0]},
    {"id": 3, "area": "slow", "cost": 3.0, "animation": {"frames": [3, 4, 5], "fps": 2}}
  ]
})";

Tileset loadTileset() {
    auto parsed = Json::parse(tilesetText);
    CHECK(parsed);
    auto set = Tileset::fromJson(parsed.value());
    CHECK(set);
    return set ? set.value() : Tileset{};
}

void tilesets() {
    const Tileset set = loadTileset();
    CHECK(set.name == "Test" && set.tileCount() == 8 && set.tiles.size() == 4);
    CHECK(!set.properties(0).solid && set.properties(1).solid && set.properties(1).opaque);
    CHECK_NEAR(set.properties(1).noiseDamping, 0.8);
    CHECK(set.properties(1).modify.action == "Dig" && set.properties(1).modify.result == 0 &&
          set.properties(1).modify.material == "dirt");
    CHECK(set.properties(2).collider && set.properties(2).collider->size.x == 0.2F);
    CHECK(set.properties(3).area == "slow" && set.properties(3).cost == 3.0F);
    CHECK(!set.properties(7).solid && set.properties(7).cost == 1.0F); // Not listed: the defaults.
    // Sheet geometry: tile 5 is column 1, row 1.
    const Rect five = set.sourceRect(5);
    CHECK(five.position == Vec2{16.0F, 16.0F} && five.size == Vec2{16.0F, 16.0F});
    // Animated tiles step through their frames; others stay.
    CHECK(set.frameAt(3, 0.0) == 3 && set.frameAt(3, 0.4) == 3 && set.frameAt(3, 0.6) == 4 &&
          set.frameAt(3, 1.2) == 5 && set.frameAt(3, 1.6) == 3 && set.frameAt(3, 2.1) == 4);
    CHECK(set.frameAt(1, 9.0) == 1);
    // A round trip through JSON keeps everything.
    auto again = Tileset::fromJson(set.toJson());
    CHECK(again && again.value().tiles == set.tiles && again.value().texture == set.texture);
    // Margin and spacing move the sources.
    Tileset spaced = set;
    spaced.margin = 2;
    spaced.spacing = 1;
    CHECK(spaced.sourceRect(1).position == Vec2{19.0F, 2.0F});
}

void tilesetErrors() {
    const auto load = [](const std::string &text) {
        auto parsed = Json::parse(text);
        return parsed ? Tileset::fromJson(parsed.value()) : Result<Tileset>(Error{parsed.error()});
    };
    const std::string base = R"("format":"yk.tileset","version":1,"columns":2,"rows":2)";
    CHECK(load("{" + base + "}"));
    CHECK(!load("[]") && !load(R"({"version":1})"));
    CHECK(!load(R"({"format":"yk.tileset"})")); // No version.
    const auto future = load(R"({"format":"yk.tileset","version":99})");
    CHECK(!future && future.error().find("newer") != std::string::npos);
    const auto outside = load("{" + base + R"(,"tiles":[{"id":9}]})");
    CHECK(!outside && outside.error().find("outside the sheet") != std::string::npos);
    CHECK(!load("{" + base + R"(,"tiles":[{"id":1},{"id":1}]})")); // Twice.
    CHECK(!load("{" + base + R"(,"tiles":[{"solid":true}]})"));    // No id.
    CHECK(!load("{" + base + R"(,"tiles":[{"id":0,"cost":0}]})"));
    CHECK(!load("{" + base + R"(,"tiles":[{"id":0,"noiseDamping":2}]})"));
    CHECK(!load("{" + base + R"(,"tiles":[{"id":0,"collider":[0,0,2,1]}]})"));
    CHECK(!load("{" + base + R"(,"tiles":[{"id":0,"collider":[0,0]}]})"));
    CHECK(!load("{" + base + R"(,"tiles":[{"id":0,"animation":{"frames":[7]}}]})"));
    CHECK(!load("{" + base + R"(,"tiles":[{"id":0,"modify":{"action":"Dig","result":9}}]})"));
    CHECK(!load("{" + base + R"(,"tiles":[{"id":0,"modify":{"resistance":5}}]})"));
    CHECK(!load(R"({"format":"yk.tileset","version":1,"columns":0,"rows":1})"));
    CHECK(!load(R"({"format":"yk.tileset","version":1,"tileWidth":"big"})"));
    CHECK(!load("{" + base + R"(,"tiles":{"a":1}})"));
    // Unknown keys are ignored (older programs open newer files that gained optional fields).
    CHECK(load("{" + base + R"(,"somethingNew":[1,2,3]})"));
}

void layers() {
    TileLayer layer;
    CHECK(layer.isEmpty() && layer.cell(3, 4) == 0 && layer.tileCount() == 0);
    CHECK(layer.setCell(3, 4, tile::make(1)));
    CHECK(!layer.setCell(3, 4, tile::make(1))); // Unchanged.
    CHECK(layer.cell(3, 4) == tile::make(1) && layer.tileCount() == 1);
    // Negative coordinates belong to chunks of their own.
    CHECK(layer.setCell(-1, -1, tile::make(2, tile::flipX)));
    CHECK(layer.cell(-1, -1) == tile::make(2, tile::flipX));
    CHECK(tile::index(layer.cell(-1, -1)) == 2 && tile::flags(layer.cell(-1, -1)) == tile::flipX);
    CHECK(layer.chunks().size() == 2 && layer.chunks().contains({-1, -1}) &&
          layer.chunks().contains({0, 0}));
    CHECK(layer.setCell(40, 0, tile::make(0)));
    CHECK(layer.chunks().size() == 3 && layer.chunks().contains({2, 0}));
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    CHECK(layer.bounds(minX, minY, maxX, maxY) && minX == -1 && minY == -1 && maxX == 40 &&
          maxY == 4);
    // Erasing the last tile of a chunk drops the chunk.
    CHECK(layer.setCell(40, 0, 0) && layer.chunks().size() == 2);
    CHECK(!layer.setCell(500, 500, 0)); // Erasing nothing changes nothing and adds no chunk.
    CHECK(layer.chunks().size() == 2);
    // JSON: runs, and a full chunk of one tile is a single run.
    TileLayer floor = TileLayer::make("Floor", TileLayerKind::Floor);
    for (int y = 0; y < tileChunkSize; ++y)
        for (int x = 0; x < tileChunkSize; ++x)
            floor.setCell(x, y, tile::make(0));
    const Json saved = floor.toJson();
    CHECK(saved.get("chunks").size() == 1 && saved.get("chunks").at(0).get("runs").size() == 2);
    auto loaded = TileLayer::fromJson(saved);
    CHECK(loaded && loaded.value() == floor);
    layer.name = "Walls";
    layer.solid = true;
    layer.ySort = true;
    layer.level = "upstairs";
    layer.opacity = 0.5F;
    auto round = TileLayer::fromJson(layer.toJson());
    CHECK(round && round.value() == layer && round.value().level == "upstairs");
    // Layer kinds start with sensible behavior.
    CHECK(TileLayer::make("W", TileLayerKind::Wall).solid &&
          TileLayer::make("W", TileLayerKind::Wall).ySort);
    CHECK(!TileLayer::make("F", TileLayerKind::Floor).solid &&
          !TileLayer::make("C", TileLayerKind::Collision).visible);
    CHECK(TileLayer::make("R", TileLayerKind::Roof).sortLayer > 0);
}

void layerErrors() {
    const auto load = [](const std::string &text) {
        auto parsed = Json::parse(text);
        return parsed ? TileLayer::fromJson(parsed.value())
                      : Result<TileLayer>(Error{parsed.error()});
    };
    CHECK(load(R"({"name":"A"})"));
    CHECK(!load("[]") && !load("{}"));
    CHECK(!load(R"({"name":"A","kind":"Moon"})"));
    CHECK(!load(R"({"name":"A","opacity":2})"));
    CHECK(!load(R"({"name":"A","chunks":5})"));
    CHECK(!load(R"({"name":"A","chunks":[{"y":0,"runs":[1,256]}]})"));           // No x.
    CHECK(!load(R"({"name":"A","chunks":[{"x":0,"y":0,"runs":[1]}]})"));         // Odd length.
    CHECK(!load(R"({"name":"A","chunks":[{"x":0,"y":0,"runs":[1,100]}]})"));     // Short.
    CHECK(!load(R"({"name":"A","chunks":[{"x":0,"y":0,"runs":[1,300]}]})"));     // Long.
    CHECK(!load(R"({"name":"A","chunks":[{"x":0,"y":0,"runs":[1,0,1,256]}]})")); // Zero count.
    CHECK(!load(R"({"name":"A","chunks":[{"x":0,"y":0,"runs":[-1,256]}]})"));
    CHECK(!load(
        R"({"name":"A","chunks":[{"x":0,"y":0,"runs":[1,256]},{"x":0,"y":0,"runs":[1,256]}]})"));
    const auto short_ = load(R"({"name":"Walls","chunks":[{"x":0,"y":0,"runs":[1,100]}]})");
    CHECK(!short_ && short_.error().find("Walls") != std::string::npos &&
          short_.error().find("100 of 256") != std::string::npos);
}

ComponentRegistry registry() {
    ComponentRegistry result;
    registerEngineComponents(result);
    return result;
}

void component() {
    ComponentRegistry reg = registry();
    Scene scene(reg, 3);
    Entity &entity = scene.createEntity("Map");
    entity.transform().position = {10.0F, 20.0F};
    auto &map = entity.add<Tilemap>();
    map.tileset.path = "assets/test.yktileset";
    map.cellSize = {2.0F, 1.0F};
    const std::size_t floor = map.addLayer(TileLayer::make("Floor", TileLayerKind::Floor));
    const std::size_t walls = map.addLayer(TileLayer::make("Walls", TileLayerKind::Wall));
    CHECK(floor == 0 && walls == 1);
    // Geometry follows the entity: position is the corner of cell (0,0), cells are 2 x 1 meters.
    CHECK(map.cellToWorld(0, 0) == Vec2{10.0F, 20.0F} &&
          map.cellToWorld(3, 2) == Vec2{16.0F, 22.0F});
    CHECK(map.cellCenter(0, 0) == Vec2{11.0F, 20.5F});
    int cx = 0, cy = 0;
    map.worldToCell({16.5F, 22.2F}, cx, cy);
    CHECK(cx == 3 && cy == 2);
    map.worldToCell({9.0F, 19.0F}, cx, cy); // Left of and above the origin: negative cells.
    CHECK(cx == -1 && cy == -1);
    entity.transform().scale = {2.0F, 2.0F}; // Scale multiplies the cell size.
    CHECK(map.cellSizeInWorld() == Vec2{4.0F, 2.0F});
    entity.transform().scale = {1.0F, 1.0F};
    // Edits are logged, in order, for whoever mirrors the map.
    const std::uint64_t start = map.sequence();
    CHECK(map.setTile(walls, 1, 1, tile::make(1)) && map.setTile(walls, 2, 1, tile::make(1)));
    CHECK(!map.setTile(walls, 2, 1, tile::make(1))); // Unchanged: no log entry.
    CHECK(!map.setTile(9, 0, 0, tile::make(1)));     // No such layer.
    bool complete = false;
    auto changes = map.changesSince(start, complete);
    CHECK(complete && changes.size() == 2 && changes[0].x == 1 && changes[1].x == 2 &&
          changes[0].before == 0 && changes[0].after == tile::make(1));
    CHECK(map.changesSince(map.sequence(), complete).empty() && complete);
    // A reload of the data is not an edit: observers must rebuild.
    CHECK(map.setLayersFromJson(map.layersToJson()));
    map.changesSince(start, complete);
    CHECK(!complete);
    // Bad data changes nothing and says why.
    const Json before = map.layersToJson();
    CHECK(!map.setLayersFromJson(Json::parse("{\"x\":1}").value()) && !map.lastError().empty());
    CHECK(!map.setLayersFromJson(Json::parse("[{\"name\":\"A\",\"kind\":\"Moon\"}]").value()));
    CHECK(map.layersToJson() == before);
    // The map is saved with the scene and comes back the same (the Json property).
    const Json saved = sceneToJson(scene);
    auto loaded = sceneFromJson(saved, reg);
    CHECK(loaded);
    if (loaded) {
        const auto *again = loaded.value()->findByName("Map")->get<Tilemap>();
        CHECK(again && again->layers == map.layers && again->cellSize == map.cellSize &&
              again->tileset.path == map.tileset.path);
        CHECK(sceneToJson(*loaded.value()) == saved);
    }
    // The component reports what is wrong with itself.
    Entity &bare = scene.createEntity("Bare");
    auto &empty = bare.add<Tilemap>();
    empty.addLayer(TileLayer::make("A", TileLayerKind::Floor));
    TileLayer lost = TileLayer::make("A", TileLayerKind::Floor);
    lost.level = "nowhere";
    scene.settings.levels.levels = {{"ground", "", LevelKind::Floor, 0.0F}};
    empty.addLayer(lost);
    std::vector<std::string> problems;
    const ComponentType *type = reg.find("Tilemap");
    CHECK(type && type->check);
    type->check(bare, empty, CheckContext{}, problems);
    CHECK(problems.size() == 3);
    const auto mentions = [&](const char *text) {
        return std::any_of(problems.begin(), problems.end(), [&](const std::string &line) {
            return line.find(text) != std::string::npos;
        });
    };
    CHECK(mentions("no tileset") && mentions("'nowhere'") && mentions("both called 'A'"));
}

// ---- Physics
// -------------------------------------------------------------------------------------
struct Level {
    ComponentRegistry reg = registry();
    MemoryAssets assets;
    std::unique_ptr<Scene> scene = std::make_unique<Scene>(reg, 9);
    std::unique_ptr<GameRuntime> runtime;
    EntityId map, ball;
    // A floor row of wall tiles at y = 10 across 40 cells (three chunks wide), and a crate above.
    // `slider`: the crate starts on the floor and cannot turn, for sliding it into things.
    explicit Level(const char *wallLevel = "", bool slider = false) {
        assets.files["assets/test.yktileset"] = tilesetText;
        if (std::string(wallLevel) != "")
            scene->settings.levels.levels = {{"ground", "", LevelKind::Floor, 0.0F},
                                             {"upstairs", "", LevelKind::Floor, 3.0F}};
        Entity &entity = scene->createEntity("Map");
        auto &tiles = entity.add<Tilemap>();
        tiles.tileset.path = "assets/test.yktileset";
        tiles.collisionLayer = "Default";
        TileLayer walls = TileLayer::make("Walls", TileLayerKind::Wall);
        walls.level = wallLevel;
        const std::size_t layer = tiles.addLayer(walls);
        for (int x = 0; x < 40; ++x)
            tiles.setTile(layer, x, 10, tile::make(1));
        map = entity.id();
        Entity &crate = scene->createEntity("Ball");
        crate.transform().position = {5.5F, slider ? 9.6F : 5.0F};
        crate.add<RigidBody>().fixedRotation = slider;
        crate.add<Collider>().size = {0.8F, 0.8F};
        ball = crate.id();
        RuntimeOptions options;
        options.assets = &assets;
        setLogStderrEnabled(false);
        auto created = GameRuntime::create(std::move(scene), options);
        setLogStderrEnabled(true);
        CHECK(created);
        runtime = std::move(created.value());
    }
    float ballY() const {
        return runtime->scene().find(ball)->worldPosition().y;
    }
    Tilemap &tiles() {
        return *runtime->scene().find(map)->get<Tilemap>();
    }
    void run(int ticks) {
        for (int i = 0; i < ticks; ++i)
            runtime->stepOnce(Keyboard{});
    }
};

void solidTilesCollide() {
    Level level;
    level.run(150);
    // The floor tiles occupy y 10..11; the 0.8 m crate comes to rest on top of them.
    CHECK_NEAR(level.ballY(), 9.6, 0.05);
    // Tiles the tileset does not call solid do not collide: a floor tile row lets it through.
    Level open;
    for (int x = 0; x < 40; ++x)
        open.tiles().setTile(0, x, 10, tile::make(0)); // Floor tiles replace the walls.
    open.run(150);
    CHECK(open.ballY() > 12.0F);
}

void tilesChangeTheWorld() {
    Level level;
    level.run(120);
    CHECK_NEAR(level.ballY(), 9.6, 0.05);
    // Digging out the tiles under the crate opens the floor on the next tick (the chunk is
    // rebuilt) and a new floor four rows lower stops it there, in the same edit.
    Tilemap &map = level.tiles();
    for (int x = 4; x <= 7; ++x) {
        map.setTile(0, x, 10, 0);
        map.setTile(0, x, 14, tile::make(1));
    }
    level.run(150);
    CHECK_NEAR(level.ballY(), 13.6, 0.1);
    // With no new floor it keeps falling.
    for (int x = 4; x <= 7; ++x)
        map.setTile(0, x, 14, 0);
    level.run(120);
    CHECK(level.ballY() > 20.0F);
}

void tileLevelsFilter() {
    // A wall layer that belongs to another level does not stop what is on this one.
    Level other("upstairs");
    other.run(150);
    CHECK(other.ballY() > 12.0F); // The crate is on the first level; the floor is upstairs.
    Level same("ground");
    same.run(150);
    CHECK_NEAR(same.ballY(), 9.6, 0.05);
}

void partialColliders() {
    // A fence post (tile 2) blocks only the middle fifth of its cell: a crate sliding along the
    // floor stops against its face (x 8.4), not at the edge of the cell (x 8.0).
    Level level("", true);
    level.tiles().setTile(0, 8, 9, tile::make(2));
    level.run(30);
    level.runtime->physics().setVelocity(*level.runtime->bodyOf(level.ball), {3.0F, 0.0F});
    level.run(120);
    const float x = level.runtime->scene().find(level.ball)->worldPosition().x;
    CHECK_NEAR(x, 8.4 - 0.4, 0.06);
    // Take the post away and the crate slides on past.
    level.tiles().setTile(0, 8, 9, 0);
    level.runtime->physics().setVelocity(*level.runtime->bodyOf(level.ball), {3.0F, 0.0F});
    level.run(60);
    CHECK(level.runtime->scene().find(level.ball)->worldPosition().x > 9.5F);
}

void chunksAreMerged() {
    // A long wall is a few shapes, not one per tile: 40 tiles over three chunks (16 + 16 + 8).
    Level level;
    std::size_t shapes = 0;
    level.runtime->physics().stats();
    shapes = level.runtime->physics().stats().shapes;
    CHECK(shapes <= 4); // The crate's shape plus at most three merged boxes.
    CHECK(level.runtime->physics().stats().bodies <= 5);
}

void missingTileset() {
    // A map whose tileset cannot be read is reported and does not stop the game.
    ComponentRegistry reg = registry();
    auto scene = std::make_unique<Scene>(reg, 4);
    Entity &entity = scene->createEntity("Map");
    auto &tiles = entity.add<Tilemap>();
    tiles.tileset.path = "assets/missing.yktileset";
    tiles.addLayer(TileLayer::make("Walls", TileLayerKind::Wall));
    tiles.setTile(0, 0, 0, tile::make(1));
    MemoryAssets assets;
    RuntimeOptions options;
    options.assets = &assets;
    setLogStderrEnabled(false);
    auto created = GameRuntime::create(std::move(scene), options);
    CHECK(created);
    if (created)
        for (int i = 0; i < 5; ++i)
            created.value()->stepOnce(Keyboard{});
    setLogStderrEnabled(true);
}
} // namespace

int main() {
    tilesets();
    tilesetErrors();
    layers();
    layerErrors();
    component();
    solidTilesCollide();
    tilesChangeTheWorld();
    tileLevelsFilter();
    partialColliders();
    chunksAreMerged();
    missingTileset();
    return yk::test::finish("tilemap");
}
