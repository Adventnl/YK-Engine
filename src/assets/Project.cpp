#include "yk/assets/Project.hpp"
#include "yk/core/FileIO.hpp"
#include <algorithm>

namespace yk {
LayerConfig LayerConfig::defaults() {
    return {{"Default"}, {1U}};
}
int LayerConfig::indexOf(std::string_view name) const {
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i] == name)
            return static_cast<int>(i);
    return -1;
}
std::uint64_t LayerConfig::categoryBits(std::string_view name) const {
    const int index = std::max(indexOf(name), 0);
    return std::uint64_t{1} << index;
}
std::uint64_t LayerConfig::maskBits(std::string_view name) const {
    const int index = std::max(indexOf(name), 0);
    return static_cast<std::size_t>(index) < masks.size() ? masks[static_cast<std::size_t>(index)]
                                                          : 0;
}
bool LayerConfig::interacts(std::size_t a, std::size_t b) const {
    return a < masks.size() && b < masks.size() && ((masks[a] >> b) & 1U) != 0;
}
void LayerConfig::setInteraction(std::size_t a, std::size_t b, bool interacts) {
    if (a >= masks.size() || b >= masks.size())
        return;
    const auto apply = [&](std::size_t from, std::size_t to) {
        if (interacts)
            masks[from] |= 1U << to;
        else
            masks[from] &= ~(1U << to);
    };
    apply(a, b);
    apply(b, a);
}
Status LayerConfig::addLayer(std::string name) {
    if (names.size() >= maxLayers)
        return Error{"At most " + std::to_string(maxLayers) + " collision layers are supported"};
    if (name.empty() || indexOf(name) >= 0)
        return Error{"Layer names must be non-empty and unique"};
    names.push_back(std::move(name));
    masks.push_back(0);
    return success();
}
Status LayerConfig::rename(std::size_t index, std::string name) {
    if (index >= names.size())
        return Error{"Unknown layer index"};
    if (name.empty() || (indexOf(name) >= 0 && indexOf(name) != static_cast<int>(index)))
        return Error{"Layer names must be non-empty and unique"};
    names[index] = std::move(name);
    return success();
}
Status LayerConfig::validate() const {
    if (names.empty() || names.size() > maxLayers || names.size() != masks.size())
        return Error{"Layer configuration must have 1-32 named layers with one mask each"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty() || indexOf(names[i]) != static_cast<int>(i))
            return Error{"Layer names must be non-empty and unique"};
        for (std::size_t j = 0; j < names.size(); ++j)
            if (interacts(i, j) != interacts(j, i))
                return Error{"Layer interactions must be symmetric ('" + names[i] + "' / '" +
                             names[j] + "')"};
        if (names.size() < 32 && (masks[i] >> names.size()) != 0)
            return Error{"Layer '" + names[i] + "' interacts with a layer that does not exist"};
    }
    return success();
}
Json LayerConfig::toJson() const {
    Json array = Json::array();
    for (std::size_t i = 0; i < names.size(); ++i) {
        Json layer = Json::object();
        layer.set("name", names[i]);
        Json partners = Json::array();
        for (std::size_t j = 0; j < names.size(); ++j)
            if (interacts(i, j))
                partners.push(names[j]);
        layer.set("interactsWith", partners);
        array.push(std::move(layer));
    }
    return array;
}
Result<LayerConfig> LayerConfig::fromJson(const Json &json) {
    if (!json.isArray() || json.size() == 0)
        return Error{"'layers' must be a non-empty array"};
    LayerConfig config;
    for (const Json &layer : json.items()) {
        if (!layer.isObject() || !layer.get("name").isString())
            return Error{"Each layer needs a 'name' string"};
        if (auto status = config.addLayer(layer.get("name").asString()); !status)
            return Error{status.error() + " ('" + layer.get("name").asString() + "')"};
    }
    for (std::size_t i = 0; i < json.size(); ++i) {
        for (const Json &partner : json.at(i).get("interactsWith").items()) {
            const int other = config.indexOf(partner.asString());
            if (other < 0)
                return Error{"Layer '" + config.names[i] + "' refers to unknown layer '" +
                             partner.asString() + "'"};
            config.setInteraction(i, static_cast<std::size_t>(other), true);
        }
    }
    if (auto status = config.validate(); !status)
        return Error{status.error()};
    return config;
}

Project Project::create(const std::filesystem::path &directory, std::string projectName) {
    Project project;
    project.name = std::move(projectName);
    project.root = std::filesystem::absolute(directory).lexically_normal();
    project.window.title = project.name;
    return project;
}
Result<Project> Project::load(const std::filesystem::path &fileOrDirectory) {
    std::filesystem::path file = fileOrDirectory;
    std::error_code error;
    if (std::filesystem::is_directory(file, error))
        file /= fileName;
    auto text = readTextFile(file);
    if (!text)
        return Error{text.error()};
    auto document = Json::parse(text.value());
    if (!document)
        return Error{file.string() + ": " + document.error()};
    const Json &json = document.value();
    if (json.get("format").asString() != "yk.project")
        return Error{file.string() + " is not a yk.project document"};
    if (!json.get("version").isNumber() || json.get("version").asInt() < 1 ||
        json.get("version").asInt() > formatVersion)
        return Error{file.string() + ": unsupported project version"};
    Project project;
    project.root = std::filesystem::absolute(file.parent_path()).lexically_normal();
    project.name = json.get("name").isString() ? json.get("name").asString() : "Untitled Project";
    project.startScene = json.get("startScene").asString();
    project.window.title = project.name;
    if (const Json *window = json.find("window")) {
        if (window->get("title").isString())
            project.window.title = window->get("title").asString();
        const auto width = window->get("width").asInt(project.window.width);
        const auto height = window->get("height").asInt(project.window.height);
        if (width < 160 || height < 120 || width > 16384 || height > 16384)
            return Error{file.string() + ": window size is out of range"};
        project.window.width = static_cast<int>(width);
        project.window.height = static_cast<int>(height);
    }
    if (const Json *layers = json.find("layers")) {
        auto parsed = LayerConfig::fromJson(*layers);
        if (!parsed)
            return Error{file.string() + ": " + parsed.error()};
        project.layers = std::move(parsed.value());
    }
    return project;
}
Status Project::save() const {
    Json json = Json::object();
    json.set("format", "yk.project");
    json.set("version", formatVersion);
    json.set("name", name);
    json.set("startScene", startScene);
    Json windowJson = Json::object();
    windowJson.set("title", window.title);
    windowJson.set("width", window.width);
    windowJson.set("height", window.height);
    json.set("window", windowJson);
    json.set("layers", layers.toJson());
    return writeTextFileAtomic(file(), json.dump(2) + "\n");
}
Result<std::filesystem::path> Project::resolve(std::string_view relative) const {
    if (relative.empty())
        return Error{"Empty asset path"};
    const std::filesystem::path path{std::string(relative)};
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return Error{"Asset path must be project-relative: " + std::string(relative)};
    for (const auto &part : path)
        if (part == "..")
            return Error{"Asset path must not leave the project: " + std::string(relative)};
    return (root / path).lexically_normal();
}
std::optional<std::string> Project::relativize(const std::filesystem::path &absolute) const {
    std::error_code error;
    const auto relative = std::filesystem::relative(absolute, root, error);
    if (error || relative.empty() || relative.native().starts_with(".."))
        return std::nullopt;
    return toPortablePath(relative);
}
} // namespace yk
