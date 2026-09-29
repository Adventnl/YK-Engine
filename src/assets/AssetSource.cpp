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
    return AssetKind::Other;
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
