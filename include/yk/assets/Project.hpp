#pragma once
#include "yk/assets/TextureMeta.hpp"
#include "yk/core/Json.hpp"
#include "yk/core/Result.hpp"
#include "yk/input/InputMap.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yk {
// Named collision layers and which pairs interact. Interaction covers solid collision and trigger
// overlap alike (both shapes must accept the other's layer). Components refer to layers by name so
// adding or reordering layers can never silently retarget existing scenes.
struct LayerConfig {
    static constexpr std::size_t maxLayers = 32;
    std::vector<std::string> names;
    std::vector<std::uint32_t>
        masks; // masks[i] bit j: layer i interacts with layer j. Kept symmetric.

    static LayerConfig defaults(); // One layer, "Default", interacting with itself.
    std::size_t size() const {
        return names.size();
    }
    int indexOf(std::string_view name) const; // -1 when unknown.
    // Shape filter bits for a named layer; unknown names fall back to layer 0.
    std::uint64_t categoryBits(std::string_view name) const;
    std::uint64_t maskBits(std::string_view name) const;
    bool interacts(std::size_t a, std::size_t b) const;
    void setInteraction(std::size_t a, std::size_t b, bool interacts);
    Status addLayer(std::string name); // New layers interact with nothing until configured.
    Status rename(std::size_t index, std::string name);
    Status validate() const;

    Json toJson() const;
    static Result<LayerConfig> fromJson(const Json &json);
};

struct WindowSettings {
    std::string title{"YK Game"};
    int width{1280};
    int height{720};
};

// How the game is named and packaged when it is exported (`yk export`, Build > Export Game).
struct BuildSettings {
    std::string productName; // Shown to players (folder, bundle, README). Empty: the project name.
    std::string executable; // Program file name without extension. Empty: derived from the product.
    std::string version{"1.0.0"};
    std::string identifier; // macOS bundle identifier ("com.studio.game"). Empty: derived.
    // Project-relative files or folders left out of the game, on top of the ones that are never
    // shipped (tools/, docs/, scripts, notes, backup files).
    std::vector<std::string> exclude;

    Json toJson() const;
    static Result<BuildSettings> fromJson(const Json &json);
    friend bool operator==(const BuildSettings &, const BuildSettings &) = default;
};

// A directory of scenes, prefabs and assets described by project.ykproj. Every path stored in
// project data is project-relative with '/' separators.
class Project {
  public:
    static constexpr const char *fileName = "project.ykproj";
    static constexpr int formatVersion = 1;

    std::string name{"Untitled Project"};
    std::filesystem::path root; // Absolute directory containing project.ykproj.
    std::string startScene;     // Project-relative .ykscene, empty when none.
    WindowSettings window;
    LayerConfig layers{LayerConfig::defaults()};
    // Named actions and the keys/buttons that drive them (see yk/input/InputMap.hpp).
    InputMap input{InputMap::standard()};
    // Fallbacks for textures without a .ykmeta sidecar (world scale and filtering).
    TextureDefaults textures;
    BuildSettings build;

    // Accepts the project file or its directory.
    static Result<Project> load(const std::filesystem::path &fileOrDirectory);
    static Project create(const std::filesystem::path &root, std::string name);
    Status save() const;
    Json toJson() const; // The project.ykproj document.

    std::filesystem::path file() const {
        return root / fileName;
    }
    // Absolute path for a project-relative path. Rejects absolute paths and any ".." escape.
    Result<std::filesystem::path> resolve(std::string_view relative) const;
    // Project-relative portable path, or nullopt when `absolute` lies outside the project.
    std::optional<std::string> relativize(const std::filesystem::path &absolute) const;
};
} // namespace yk
