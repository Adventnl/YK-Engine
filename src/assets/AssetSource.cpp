#include "yk/assets/AssetSource.hpp"
#include "yk/core/FileIO.hpp"
#include <algorithm>

namespace yk {
Result<std::string> ProjectAssets::readText(const std::string &path) const {
    auto resolved = project_->resolve(path);
    if (!resolved)
        return Error{resolved.error()};
    return readTextFile(resolved.value());
}
std::filesystem::path ProjectAssets::filePath(const std::string &path) const {
    auto resolved = project_->resolve(path);
    return resolved ? resolved.value() : std::filesystem::path{};
}
std::vector<std::string> ProjectAssets::list(std::string_view extension) const {
    std::vector<std::string> paths;
    for (const AssetEntry &entry : scanAssets(*project_))
        if (std::string_view(entry.path).ends_with(extension))
            paths.push_back(entry.path);
    return paths; // scanAssets sorts.
}
std::vector<std::string> MemoryAssets::list(std::string_view extension) const {
    std::vector<std::string> paths;
    for (const auto &[path, text] : files) {
        (void)text;
        if (std::string_view(path).ends_with(extension))
            paths.push_back(path);
    }
    return paths; // A map: already sorted.
}
Result<std::string> MemoryAssets::readText(const std::string &path) const {
    const auto found = files.find(path);
    if (found == files.end())
        return Error{"No such asset '" + path + "'"};
    return found->second;
}

AssetKind classifyAsset(const std::string &path) {
    const auto extension = std::filesystem::path(path).extension().string();
    if (extension == ".ykscene")
        return AssetKind::Scene;
    if (extension == ".ykprefab")
        return AssetKind::Prefab;
    if (extension == ".png" || extension == ".bmp")
        return AssetKind::Texture;
    if (extension == ".wav")
        return AssetKind::Sound;
    if (extension == ".ykanim")
        return AssetKind::Animation;
    if (extension == ".ykctl")
        return AssetKind::Controller;
    if (extension == ".ykdialogue")
        return AssetKind::Dialogue;
    if (extension == ".ykmeta")
        return AssetKind::TextureMeta;
    if (extension == ".yktileset")
        return AssetKind::Tileset;
    if (extension == ".ykdata")
        return AssetKind::Data;
    if (extension == ".ykitem")
        return AssetKind::Item;
    if (extension == ".ykrecipe")
        return AssetKind::Recipe;
    if (extension == ".ykloot")
        return AssetKind::Loot;
    if (extension == ".ykquest")
        return AssetKind::Quest;
    if (extension == ".ykschedule")
        return AssetKind::Schedule;
    if (extension == ".ykscript")
        return AssetKind::Script;
    if (extension == ".ykseq")
        return AssetKind::Sequence;
    return AssetKind::Other;
}
const char *assetKindName(AssetKind kind) {
    switch (kind) {
    case AssetKind::Scene:
        return "scene";
    case AssetKind::Prefab:
        return "prefab";
    case AssetKind::Texture:
        return "texture";
    case AssetKind::Sound:
        return "sound";
    case AssetKind::Animation:
        return "animation";
    case AssetKind::Controller:
        return "controller";
    case AssetKind::Dialogue:
        return "dialogue";
    case AssetKind::TextureMeta:
        return "texture meta";
    case AssetKind::Tileset:
        return "tileset";
    case AssetKind::Data:
        return "data";
    case AssetKind::Item:
        return "item";
    case AssetKind::Recipe:
        return "recipe";
    case AssetKind::Loot:
        return "loot";
    case AssetKind::Quest:
        return "quest";
    case AssetKind::Schedule:
        return "schedule";
    case AssetKind::Script:
        return "script";
    case AssetKind::Sequence:
        return "sequence";
    case AssetKind::Other:
        return "other";
    }
    return "other";
}
std::vector<AssetEntry> scanAssets(const Project &project) {
    std::vector<AssetEntry> entries;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(project.root, error), end;
         !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (it->is_directory(error) && (name.starts_with(".") || name == "build")) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(error) || name.starts_with("."))
            continue;
        if (const auto relative = project.relativize(it->path()))
            entries.push_back({*relative, classifyAsset(*relative)});
    }
    std::sort(entries.begin(), entries.end(),
              [](const AssetEntry &a, const AssetEntry &b) { return a.path < b.path; });
    return entries;
}
} // namespace yk
