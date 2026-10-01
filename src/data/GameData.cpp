#include "yk/data/GameData.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameContext.hpp"

namespace yk {
namespace {
constexpr int dataFormatVersion = 1;

// Collects what the rule validator finds in definition files as problems of those files.
class FileReport final : public RuleReport {
  public:
    FileReport(const GameData &data, std::vector<DataProblem> &problems)
        : data_(data), problems_(problems) {}
    void at(const RuleSource &source) {
        file_ = source.file;
        label_ = source.label;
    }
    void error(const std::string &message) override {
        problems_.push_back({file_, label_ + ": " + message, true});
    }
    void warning(const std::string &message) override {
        problems_.push_back({file_, label_ + ": " + message, false});
    }
    bool known(std::string_view kind, std::string_view id) const override {
        return data_.known(kind, id);
    }

  private:
    const GameData &data_;
    std::vector<DataProblem> &problems_;
    std::string file_, label_;
};
} // namespace

const std::vector<std::string> &GameData::extensions() {
    static const std::vector<std::string> list{".ykdata", ".ykitem"};
    return list;
}

GameData GameData::load(const AssetSource &assets, std::vector<DataProblem> &problems) {
    GameData data;
    for (const std::string &extension : extensions())
        for (const std::string &path : assets.list(extension)) {
            auto text = assets.readText(path);
            if (!text) {
                problems.push_back({path, text.error(), true});
                continue;
            }
            auto document = Json::parse(text.value());
            if (!document) {
                problems.push_back({path, document.error(), true});
                continue;
            }
            data.add(document.value(), path, problems);
        }
    data.finalize(problems);
    return data;
}

void GameData::finalize(std::vector<DataProblem> &problems) {
    items.synthesizeEffects(stats.effects, problems);
}

void GameData::add(const Json &original, const std::string &file,
                   std::vector<DataProblem> &problems) {
    // A file with a single definition may be that definition itself ({"id": "screwdriver", ...}).
    Json wrapped;
    if (original.isObject() && file.ends_with(".ykitem") && original.contains("id") &&
        !original.contains("items")) {
        wrapped = Json::object();
        Json list = Json::array();
        list.push(original);
        wrapped.set("items", list);
    }
    const Json &document = wrapped.isObject() ? wrapped : original;
    if (!document.isObject()) {
        problems.push_back(
            {file, "a definition file is a JSON object with sections ('stats', 'effects', ...)",
             true});
        return;
    }
    const std::string expected = file.ends_with(".ykitem") ? "yk.item" : "yk.data";
    if (document.contains("format") && document.get("format").asString() != expected) {
        problems.push_back({file,
                            "the format is '" + document.get("format").asString() + "', not '" +
                                expected + "'",
                            true});
        return;
    }
    if (document.get("version").asInt(dataFormatVersion) > dataFormatVersion) {
        problems.push_back(
            {file,
             "this file is version " + std::to_string(document.get("version").asInt()) +
                 " but this engine reads up to version " + std::to_string(dataFormatVersion),
             true});
        return;
    }
    std::vector<std::string> warnings;
    data::warnUnknown(
        document,
        {"format", "version", "name", "description", "stats", "effects", "items", "tables"},
        warnings);
    for (const std::string &warning : warnings)
        problems.push_back({file, warning, false});
    stats.load(document, file, problems);
    items.load(document, file, problems);
    if (document.contains("tables")) {
        if (!document.get("tables").isObject()) {
            problems.push_back({file, "'tables' must be an object of named tables", true});
        } else {
            const Json &list = document.get("tables");
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (tables.contains(list.keyAt(i)))
                    problems.push_back({file,
                                        "the table '" + list.keyAt(i) +
                                            "' is defined twice; the later one is used",
                                        false});
                tables[list.keyAt(i)] = list.valueAt(i);
            }
        }
    }
}

void GameData::check(const RuleCatalog *rules, std::vector<DataProblem> &problems) const {
    stats.check(problems);
    items.check(stats, problems);
    if (!rules)
        return;
    FileReport report(*this, problems);
    visitRules([&](const RuleSource &source) {
        report.at(source);
        if (source.condition)
            rules->check(*source.condition, report);
        if (source.actions)
            rules->check(*source.actions, report);
    });
}

bool GameData::known(std::string_view kind, std::string_view id) const {
    if (kind == "stat")
        return stats.stats.contains(id);
    if (kind == "effect")
        return stats.effects.contains(id);
    if (kind == "item")
        return items.items.contains(id);
    return true;
}

void GameData::visitRules(const RuleSourceVisitor &visit) const {
    stats.visitRules(visit);
    items.visitRules(visit);
}
void GameData::visitAssets(const AssetRefVisitor &visit) const {
    items.visitAssets(visit);
}

std::vector<std::pair<std::string, std::string>> GameData::summary() const {
    std::vector<std::pair<std::string, std::string>> rows;
    const auto count = [&](const char *label, std::size_t n) {
        if (n > 0)
            rows.push_back({label, std::to_string(n)});
    };
    count("Stats", stats.stats.size());
    count("Status effects", stats.effects.size());
    count("Items", items.items.size());
    count("Tables", tables.size());
    return rows;
}

void DataService::onStart(GameContext &context) {
    const AssetSource *assets = context.assets();
    if (!assets)
        return;
    std::vector<DataProblem> problems;
    auto loaded = GameData::load(*assets, problems);
    for (const DataProblem &problem : problems)
        log(problem.error ? LogLevel::Error : LogLevel::Warning, "data",
            problem.file + ": " + problem.message);
    data_ = std::make_shared<const GameData>(std::move(loaded));
}
void DataService::use(std::shared_ptr<const GameData> data) {
    if (data)
        data_ = std::move(data);
}

const GameData &gameData(GameContext &context) {
    return context.services().get<DataService>().data();
}
} // namespace yk
