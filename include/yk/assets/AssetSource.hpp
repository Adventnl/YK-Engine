#pragma once
#include "yk/assets/Project.hpp"
#include <map>
#include <string_view>
#include <vector>

namespace yk {
// Read access to project-relative assets. The runtime depends on this interface rather than the
// file system, so gameplay runs unchanged against a project on disk, an exported package or a test
// fixture.
class AssetSource {
  public:
    virtual ~AssetSource() = default;
    virtual Result<std::string> readText(const std::string &path) const = 0;
    // Absolute file for consumers that need a real path (image and audio decoders); empty when the
    // asset has no backing file.
    virtual std::filesystem::path filePath(const std::string &path) const = 0;
    // Every asset whose path ends in `extension` (".ykitem"), project-relative with '/' separators,
    // sorted. The game's definitions (items, recipes, quests...) are found this way, so a project
    // needs no list of its own files.
    virtual std::vector<std::string> list(std::string_view extension) const = 0;
};

class ProjectAssets final : public AssetSource {
  public:
    explicit ProjectAssets(const Project &project) : project_(&project) {}
    Result<std::string> readText(const std::string &path) const override;
    std::filesystem::path filePath(const std::string &path) const override;
    std::vector<std::string> list(std::string_view extension) const override;

  private:
    const Project *project_;
};

// In-memory assets for tests.
class MemoryAssets final : public AssetSource {
  public:
    std::map<std::string, std::string> files;
    Result<std::string> readText(const std::string &path) const override;
    std::filesystem::path filePath(const std::string &) const override {
        return {};
    }
    std::vector<std::string> list(std::string_view extension) const override;
};

enum class AssetKind {
    Scene,
    Prefab,
    Texture,
    Sound,
    Animation,
    Controller,
    Dialogue,
    TextureMeta,
    Tileset,
    Data,     // .ykdata: stats, effects and the other sections of a project's definitions
    Item,     // .ykitem
    Recipe,   // .ykrecipe
    Loot,     // .ykloot
    Quest,    // .ykquest
    Schedule, // .ykschedule
    Script,   // .ykscript (Lua)
    Sequence, // .ykseq (cutscenes)
    Other
};
struct AssetEntry {
    std::string path; // Project-relative, '/' separated.
    AssetKind kind{AssetKind::Other};
};
AssetKind classifyAsset(const std::string &path);
// Plain-language singular name of a kind ("scene", "texture meta"); every kind has one, so tools
// never index a table by the enum.
const char *assetKindName(AssetKind kind);
// Every file below the project root (skipping hidden entries and the build/ directory), sorted by
// path.
std::vector<AssetEntry> scanAssets(const Project &project);
} // namespace yk
