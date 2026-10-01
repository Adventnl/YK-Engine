#pragma once
#include "yk/assets/AssetSource.hpp"
#include "yk/runtime/Services.hpp"
#include "yk/stats/Stats.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace yk {
// Everything a project defines as data rather than code: the stats and status effects of its
// characters, and (as the modules arrive) its items, recipes, loot, quests, factions, schedules.
// Definitions live in files of the project (".ykdata" and the specialised extensions); a file may
// hold several sections and a project any number of files, which are merged. Loading reports every
// problem and keeps what is good; the validator turns the problems into errors.
class GameData {
  public:
    StatCatalog stats;
    // Named tables for what only the game knows (a prison's names, a shop's price list); scripts
    // and rules read them by name.
    std::map<std::string, Json> tables;

    // The extensions of the files that hold definitions.
    static const std::vector<std::string> &extensions();
    // Loads every definition file the source lists; problems are appended, never fatal.
    static GameData load(const AssetSource &assets, std::vector<DataProblem> &problems);
    // Adds the contents of one definition file.
    void add(const Json &document, const std::string &file, std::vector<DataProblem> &problems);
    // Checks the definitions against each other, and every rule they hold against the catalog (null
    // skips that part).
    void check(const RuleCatalog *rules, std::vector<DataProblem> &problems) const;
    // True when the project defines an id of this kind: "stat", "effect", "item", "recipe", ...
    // Kinds this data does not know answer true (nothing to say against them).
    bool known(std::string_view kind, std::string_view id) const;
    // Hands the validator every place that holds rules.
    void visitRules(const RuleSourceVisitor &visit) const;
    // How much of each kind there is, for the Inspector and `yk info` (label, count text).
    std::vector<std::pair<std::string, std::string>> summary() const;
};

// The project's data for the running game: loaded when the game starts from its assets, or handed
// over by whoever hosts it (a player keeps one across scenes).
class DataService final : public Service {
  public:
    const char *name() const override {
        return "data";
    }
    UpdatePhase phase() const override {
        return UpdatePhase::Clock;
    }
    void onStart(GameContext &context) override;
    const GameData &data() const {
        return *data_;
    }
    // Replaces the data (a host that keeps one across scenes, a test). Components look the data up
    // when they start, so this must happen before the scene starts running.
    void use(std::shared_ptr<const GameData> data);

  private:
    std::shared_ptr<const GameData> data_{std::make_shared<const GameData>()};
};
// The data of the game that `context` runs.
const GameData &gameData(GameContext &context);
} // namespace yk
