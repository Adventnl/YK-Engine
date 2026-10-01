#include "yk/data/GameData.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameContext.hpp"

namespace yk {
namespace {
constexpr int dataFormatVersion = 1;

// The kinds of definition file: the extension, the format name inside, and the sections a file of
// that kind may use in a shorter way than the general ".ykdata" form.
struct FileKind {
    const char *extension;
    const char *format;
};
constexpr FileKind fileKinds[] = {{".ykdata", "yk.data"},
                                  {".ykitem", "yk.item"},
                                  {".ykrecipe", "yk.recipe"},
                                  {".ykloot", "yk.loot"}};

const FileKind &kindOf(const std::string &file) {
    for (const FileKind &kind : fileKinds)
        if (file.ends_with(kind.extension))
            return kind;
    return fileKinds[0];
}

// A file with a single item or recipe may be that definition itself ({"id": "screwdriver", ...});
// a loot file lists "tables" and "pools". Both are rewritten to the sections of the general form.
Json normalize(const Json &original, const std::string &file) {
    if (!original.isObject())
        return original;
    const auto wrapSingle = [&](const char *extension, const char *section) -> std::optional<Json> {
        if (!file.ends_with(extension) || !original.contains("id") || original.contains(section))
            return std::nullopt;
        Json wrapped = Json::object();
        Json list = Json::array();
        list.push(original);
        wrapped.set(section, list);
        return wrapped;
    };
    if (auto item = wrapSingle(".ykitem", "items"))
        return *item;
    if (auto recipe = wrapSingle(".ykrecipe", "recipes"))
        return *recipe;
    if (file.ends_with(".ykloot") && !original.contains("lootTables") &&
        !original.contains("lootPools")) {
        Json wrapped = Json::object();
        for (std::size_t i = 0; i < original.size(); ++i) {
            const std::string &key = original.keyAt(i);
            wrapped.set(key == "tables"  ? "lootTables"
                        : key == "pools" ? "lootPools"
                                         : key,
                        original.valueAt(i));
        }
        return wrapped;
    }
    return original;
}

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
    static const std::vector<std::string> list = [] {
        std::vector<std::string> names;
        for (const FileKind &kind : fileKinds)
            names.push_back(kind.extension);
        return names;
    }();
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
    const Json document = normalize(original, file);
    if (!document.isObject()) {
        problems.push_back(
            {file, "a definition file is a JSON object with sections ('stats', 'effects', ...)",
             true});
        return;
    }
    const std::string expected = kindOf(file).format;
    if (document.contains("format") && document.get("format").asString() != expected) {
        problems.push_back(
            {file,
             "the format is '" + document.get("format").asString() + "', not '" + expected + "'",
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
    data::warnUnknown(document,
                      {"format", "version", "name", "description", "stats", "effects", "items",
                       "lootTables", "lootPools", "recipes", "tables"},
                      warnings);
    for (const std::string &warning : warnings)
        problems.push_back({file, warning, false});
    stats.load(document, file, problems);
    items.load(document, file, problems);
    loot.load(document, file, problems);
    recipes.load(document, file, problems);
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
    loot.check(items, problems);
    recipes.check(items, stats, problems);
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
    if (kind == "loot")
        return loot.tables.contains(id);
    if (kind == "loot pool")
        return loot.pools.contains(id);
    if (kind == "recipe")
        return recipes.recipes.contains(id);
    return true;
}

void GameData::visitRules(const RuleSourceVisitor &visit) const {
    stats.visitRules(visit);
    items.visitRules(visit);
    recipes.visitRules(visit);
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
    count("Recipes", recipes.recipes.size());
    count("Loot tables", loot.tables.size());
    count("Loot pools", loot.pools.size());
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
