#pragma once
#include "yk/items/Items.hpp"
#include "yk/scene/Registry.hpp"

namespace yk {
class GameContext;
class Inventory;

// A crafting recipe is data: what goes in, what comes out, what the maker must be able to do and
// where. Ingredients are named by item or by tag ("any cloth"); a tool can be an ingredient that is
// worn down instead of used up.
struct RecipeIngredient {
    std::string item; // One of these two.
    std::string tag;
    int count{1};
    bool consume{true}; // False: the item must be carried but is not used up (a tool).
    int wear{0};        // For a tool that is not consumed: how much it wears down.
    friend bool operator==(const RecipeIngredient &, const RecipeIngredient &) = default;
};
struct RecipeOutput {
    std::string item;
    int count{1};
    friend bool operator==(const RecipeOutput &, const RecipeOutput &) = default;
};

struct RecipeDefinition {
    std::string id;
    std::string file;
    std::string displayName;
    std::string description;
    std::string category;
    std::vector<RecipeIngredient> ingredients;
    std::vector<RecipeOutput> outputs;
    std::map<std::string, double> statRequirements; // Stat id -> the least the maker needs.
    std::string station;       // The kind of crafting station needed; empty: none.
    bool knownByDefault{true}; // Else it must be learned first.
    std::vector<std::string> tags;
    Condition condition; // Anything else that must hold ("requires" in the file).

    static Result<RecipeDefinition> fromJson(const Json &json, std::vector<std::string> &warnings);
    Json toJson() const;
};
using RecipeTable = DefinitionTable<RecipeDefinition>;

struct RecipeCatalog {
    RecipeTable recipes;
    void load(const Json &document, const std::string &file, std::vector<DataProblem> &problems);
    void check(const ItemCatalog &items, const StatCatalog &stats,
               std::vector<DataProblem> &problems) const;
    void visitRules(const RuleSourceVisitor &visit) const;
};

// What happened when someone tried to craft.
struct CraftResult {
    bool ok{false};
    std::string reason; // Why not, in words for the player.
    std::vector<ItemStack> outputs;
};

// A place a recipe can require: a workbench, a forge, a desk. Found by the Crafter within its
// range.
class CraftingStation final : public Component {
  public:
    std::string station{"workbench"}; // The kind, as recipes name it.
    float range{2.0F};                // How close the maker must stand.
    static void describe(TypeBuilder<CraftingStation> &type);
    void onStart(GameContext &context) override;
    void onDestroy(GameContext &context) override;
};

// A character that can make things: it knows some recipes (the default ones and what it has
// learned), and crafts from its Inventory with its stats. Raises craft.completed (source: the
// crafter; data: recipe, outputs) and craft.failed (data: recipe, reason).
class Crafter final : public Component {
  public:
    std::vector<std::string> known; // Recipes learned besides those everyone knows.
    static void describe(TypeBuilder<Crafter> &type);

    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    bool knows(GameContext &context, const std::string &recipe) const;
    // Teaches a recipe; false when there is none by that name or it was known.
    bool learn(GameContext &context, const std::string &recipe);
    bool forget(const std::string &recipe);
    // Everything it could make if it had the things: the recipes it knows.
    std::vector<const RecipeDefinition *> available(GameContext &context) const;
    // Whether it could craft the recipe now, and why not.
    CraftResult check(GameContext &context, const std::string &recipe) const;
    // Does it: ingredients are used up (or tools worn), the outputs go into the inventory and
    // what does not fit lies at the crafter's feet.
    CraftResult craft(GameContext &context, const std::string &recipe);
};

void registerCraftingRules(RuleCatalog &catalog);
} // namespace yk
