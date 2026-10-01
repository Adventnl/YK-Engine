// The examples in docs/PROJECT_FORMAT.md are real: each JSON block of the section on definition
// files is loaded by the code that reads that kind of file and must come out with no problem at
// all, so the documentation cannot describe a format the engine does not read.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/rules/Rules.hpp"
#include "yk/sim/Dialogue.hpp"
#include "yk/sim/Sequence.hpp"
#include <filesystem>
#include <string>
#include <vector>

using namespace yk;

namespace {
// The ```json blocks between two headings of the document.
std::vector<std::string> blocksBetween(const std::string &text, const std::string &from,
                                       const std::string &to) {
    std::vector<std::string> blocks;
    const std::size_t begin = text.find(from);
    const std::size_t end = text.find(to, begin);
    if (begin == std::string::npos || end == std::string::npos)
        return blocks;
    std::size_t at = begin;
    const std::string open = "```json\n";
    while ((at = text.find(open, at)) != std::string::npos && at < end) {
        const std::size_t start = at + open.size();
        const std::size_t close = text.find("```", start);
        if (close == std::string::npos)
            break;
        blocks.push_back(text.substr(start, close - start));
        at = close + 3;
    }
    return blocks;
}

void definitionExamples() {
    auto text = readTextFile(std::filesystem::path(YK_SOURCE_DIR) / "docs/PROJECT_FORMAT.md");
    CHECK(text);
    if (!text)
        return;
    const auto blocks = blocksBetween(text.value(), "## Definition files", "## Exported games");
    CHECK(blocks.size() >= 4);

    ComponentRegistry registry;
    registerEngineComponents(registry);
    const RuleCatalog *catalog = registry.extension<RuleCatalog>();
    MemoryAssets assets;
    // What the examples point at that the document does not define.
    assets.files["defs/other.ykdata"] =
        R"({"items":[{"id":"circuit_board"},{"id":"coin","stackSize":99},{"id":"screwdriver"},
                     {"id":"favor_token"}],
            "quests":[{"id":"meet_mechanic","objectives":[{"id":"talk","manual":true}]}]})";
    std::vector<DialogueGraph> graphs;
    std::vector<SequenceDefinition> sequences;
    std::size_t data = 0, quests = 0, dialogues = 0, cutscenes = 0;
    for (const std::string &block : blocks) {
        auto json = Json::parse(block);
        CHECK(json);
        if (!json)
            continue;
        std::vector<std::string> warnings;
        if (json.value().contains("nodes") || json.value().contains("pages")) {
            auto graph = DialogueGraph::fromJson(json.value(), warnings);
            CHECK(graph);
            if (graph) {
                graph.value().check(warnings);
                graphs.push_back(std::move(graph.value()));
            }
            CHECK(warnings.empty());
            ++dialogues;
        } else if (json.value().contains("cues")) {
            auto sequence = SequenceDefinition::fromJson(json.value(), warnings);
            CHECK(sequence);
            if (sequence)
                sequences.push_back(std::move(sequence.value()));
            CHECK(warnings.empty());
            ++cutscenes;
        } else if (json.value().contains("objectives")) {
            assets.files["defs/example" + std::to_string(quests++) + ".ykquest"] = block;
        } else {
            assets.files["defs/example" + std::to_string(data++) + ".ykdata"] = block;
        }
    }
    CHECK(data >= 1 && quests >= 1 && dialogues >= 1 && cutscenes >= 1);

    std::vector<DataProblem> problems;
    const GameData game = GameData::load(assets, problems);
    game.check(catalog, problems);
    for (const DataProblem &problem : problems)
        std::fprintf(stderr, "docs example: %s: %s\n", problem.file.c_str(),
                     problem.message.c_str());
    CHECK(problems.empty());
    CHECK(game.stats.stats.find("health") && game.items.items.find("guard_outfit") &&
          game.quests.quests.find("repair_autopilot"));

    // The rules inside the conversation and the cutscene are known to the catalog.
    std::vector<DataProblem> found;
    for (const DialogueGraph &graph : graphs)
        graph.visitRules("dialogue", [&](const RuleSource &source) {
            game.checkRules(*catalog, source, found);
        });
    for (const SequenceDefinition &sequence : sequences)
        sequence.visitRules("sequence", [&](const RuleSource &source) {
            game.checkRules(*catalog, source, found);
        });
    for (const DataProblem &problem : found)
        std::fprintf(stderr, "docs example: %s: %s\n", problem.file.c_str(),
                     problem.message.c_str());
    CHECK(found.empty());
}
} // namespace

int main() {
    setLogStderrEnabled(false);
    definitionExamples();
    return yk::test::finish("docs");
}
