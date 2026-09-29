#pragma once
#include "yk/assets/Project.hpp"
#include <map>

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
};

class ProjectAssets final : public AssetSource {
  public:
    explicit ProjectAssets(const Project &project) : project_(&project) {}
    Result<std::string> readText(const std::string &path) const override;
    std::filesystem::path filePath(const std::string &path) const override;

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
};

enum class AssetKind { Scene, Prefab, Texture, Sound, Animation, Other };
struct AssetEntry {
    std::string path; // Project-relative, '/' separated.
    AssetKind kind{AssetKind::Other};
};
AssetKind classifyAsset(const std::string &path);
// Every file below the project root (skipping hidden entries and the build/ directory), sorted by
// path.
std::vector<AssetEntry> scanAssets(const Project &project);
} // namespace yk
