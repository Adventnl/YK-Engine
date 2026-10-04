#include "yk/items/Crafting.hpp"
#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameContext.hpp"
#include "yk/world/SpatialIndex.hpp"
#include <algorithm>

namespace yk {
// ---- Definitions
// ---------------------------------------------------------------------------------
Result<RecipeDefinition> RecipeDefinition::fromJson(const Json &json,
                                                    std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"a recipe must be an object"};
    RecipeDefinition recipe;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{id.error()};
    recipe.id = id.value();
    if (!data::validId(recipe.id))
        return Error{"'" + recipe.id + "' is not a usable id (letters, digits, '_' and '-')"};
    recipe.displayName = data::optionalString(json, "displayName", recipe.id);
    recipe.description = data::optionalString(json, "description");
    recipe.category = data::optionalString(json, "category");
    recipe.station = data::optionalString(json, "station");
    recipe.knownByDefault = json.get("knownByDefault").asBool(true);
    recipe.tags = data::stringList(json, "tags");

    const Json &ingredients = json.get("ingredients");
    if (!ingredients.isArray() || ingredients.size() == 0)
        return Error{"'ingredients' must be a list with at least one ingredient"};
    for (std::size_t i = 0; i < ingredients.size(); ++i) {
        const Json &entry = ingredients.at(i);
        const std::string place = "ingredient " + std::to_string(i + 1) + ": ";
        if (!entry.isObject() || (entry.contains("item") == entry.contains("tag")))
            return Error{place + "name exactly one of 'item' or 'tag'"};
        RecipeIngredient ingredient;
        ingredient.item = data::optionalString(entry, "item");
        ingredient.tag = data::optionalString(entry, "tag");
        if (ingredient.item.empty() && ingredient.tag.empty())
            return Error{place + "the item or tag is empty"};
        auto count = data::number(entry, "count", 1.0, 1.0, 9999.0);
        auto wear = data::number(entry, "wear", 0.0, 0.0, 1.0e6);
        if (!count)
            return Error{place + count.error()};
        if (!wear)
            return Error{place + wear.error()};
        ingredient.count = static_cast<int>(count.value());
        ingredient.wear = static_cast<int>(wear.value());
        ingredient.consume = entry.get("consume").asBool(true);
        if (ingredient.consume && ingredient.wear > 0)
            return Error{
                place +
                "'wear' only applies to an ingredient that is not consumed (\"consume\": false)"};
        recipe.ingredients.push_back(std::move(ingredient));
    }

    const Json &single = json.get("output");
    const Json &several = json.get("outputs");
    if (single.isNull() == several.isNull())
        return Error{"name what it makes with 'output' (one) or 'outputs' (a list)"};
    const auto readOutput = [&](const Json &entry,
                                const std::string &place) -> Result<RecipeOutput> {
        if (!entry.isObject() || !entry.get("item").isString() ||
            entry.get("item").asString().empty())
            return Error{place + "needs an 'item'"};
        auto count = data::number(entry, "count", 1.0, 1.0, 9999.0);
        if (!count)
            return Error{place + count.error()};
        return RecipeOutput{entry.get("item").asString(), static_cast<int>(count.value())};
    };
    if (!single.isNull()) {
        auto output = readOutput(single, "output ");
        if (!output)
            return Error{output.error()};
        recipe.outputs.push_back(output.value());
    } else {
        if (!several.isArray() || several.size() == 0)
            return Error{"'outputs' must be a list with at least one output"};
        for (std::size_t i = 0; i < several.size(); ++i) {
            auto output = readOutput(several.at(i), "output " + std::to_string(i + 1) + " ");
            if (!output)
                return Error{output.error()};
            recipe.outputs.push_back(output.value());
        }
    }
    if (json.contains("statRequirements")) {
        const Json &needs = json.get("statRequirements");
        if (!needs.isObject())
            return Error{"'statRequirements' must be an object like {\"intellect\": 30}"};
        for (std::size_t i = 0; i < needs.size(); ++i) {
            if (!needs.valueAt(i).isNumber())
                return Error{"the requirement for '" + needs.keyAt(i) + "' must be a number"};
            recipe.statRequirements[needs.keyAt(i)] = needs.valueAt(i).asNumber();
        }
    }
    auto condition = Condition::fromJson(json.get("requires"));
    if (!condition)
        return Error{"requires: " + condition.error()};
    recipe.condition = std::move(condition.value());
    data::warnUnknown(json,
                      {"id", "displayName", "description", "category", "ingredients", "output",
                       "outputs", "statRequirements", "station", "knownByDefault", "tags",
                       "requires"},
                      warnings);
    return recipe;
}

Json RecipeDefinition::toJson() const {
    Json json = Json::object();
    json.set("id", id);
    if (displayName != id)
        json.set("displayName", displayName);
    if (!description.empty())
        json.set("description", description);
    if (!category.empty())
        json.set("category", category);
    Json list = Json::array();
    for (const RecipeIngredient &ingredient : ingredients) {
        Json entry = Json::object();
        if (!ingredient.item.empty())
            entry.set("item", ingredient.item);
        else
            entry.set("tag", ingredient.tag);
        if (ingredient.count != 1)
            entry.set("count", ingredient.count);
        if (!ingredient.consume)
            entry.set("consume", false);
        if (ingredient.wear != 0)
            entry.set("wear", ingredient.wear);
        list.push(entry);
    }
    json.set("ingredients", list);
    const auto writeOutput = [](const RecipeOutput &output) {
        Json entry = Json::object();
        entry.set("item", output.item);
        if (output.count != 1)
            entry.set("count", output.count);
        return entry;
    };
    if (outputs.size() == 1) {
        json.set("output", writeOutput(outputs.front()));
    } else {
        Json many = Json::array();
        for (const RecipeOutput &output : outputs)
            many.push(writeOutput(output));
        json.set("outputs", many);
    }
    if (!statRequirements.empty()) {
        Json needs = Json::object();
        for (const auto &[stat, least] : statRequirements)
            needs.set(stat, least);
        json.set("statRequirements", needs);
    }
    if (!station.empty())
        json.set("station", station);
    if (!knownByDefault)
        json.set("knownByDefault", false);
    if (!tags.empty())
        json.set("tags", data::toJsonList(tags));
    if (!condition.empty())
        json.set("requires", condition.toJson());
    return json;
}

void RecipeCatalog::load(const Json &document, const std::string &file,
                         std::vector<DataProblem> &problems) {
    if (document.contains("recipes"))
        recipes.load(document.get("recipes"), file, problems, "recipe");
}

void RecipeCatalog::check(const ItemCatalog &items, const StatCatalog &stats,
                          std::vector<DataProblem> &problems) const {
    for (const RecipeDefinition &recipe : recipes.all()) {
        const std::string where = "recipe '" + recipe.id + "': ";
        for (const RecipeIngredient &ingredient : recipe.ingredients) {
            if (!ingredient.item.empty() && !items.items.contains(ingredient.item))
                problems.push_back(
                    {recipe.file, where + "the ingredient '" + ingredient.item + "' is not an item",
                     true});
            if (!ingredient.tag.empty() && items.withTag(ingredient.tag).empty())
                problems.push_back({recipe.file,
                                    where + "no item has the tag '" + ingredient.tag +
                                        "', so it can never be made",
                                    true});
            if (!ingredient.consume && ingredient.wear > 0 && !ingredient.item.empty())
                if (const ItemDefinition *def = items.items.find(ingredient.item);
                    def && def->durability == 0)
                    problems.push_back(
                        {recipe.file,
                         where + "wears '" + ingredient.item + "' down but it never wears out",
                         false});
        }
        for (const RecipeOutput &output : recipe.outputs)
            if (!items.items.contains(output.item))
                problems.push_back({recipe.file,
                                    where + "it makes '" + output.item + "', which is not an item",
                                    true});
        for (const auto &[stat, least] : recipe.statRequirements) {
            (void)least;
            if (!stats.stats.contains(stat))
                problems.push_back(
                    {recipe.file, where + "it needs the stat '" + stat + "', which is not defined",
                     true});
        }
        const bool everyIngredientKept =
            std::all_of(recipe.ingredients.begin(), recipe.ingredients.end(),
                        [](const RecipeIngredient &i) { return !i.consume; });
        if (everyIngredientKept)
            problems.push_back(
                {recipe.file, where + "uses nothing up, so it can be repeated for free", false});
    }
}

void RecipeCatalog::visitRules(const RuleSourceVisitor &visit) const {
    for (const RecipeDefinition &recipe : recipes.all())
        if (!recipe.condition.empty())
            visit({recipe.file, "recipe '" + recipe.id + "' requires", &recipe.condition, nullptr});
}

// ---- Stations
// ---------------------------------------------------------------------------------------
void CraftingStation::describe(TypeBuilder<CraftingStation> &type) {
    type.category("Items").description("A place recipes can require: a workbench, a forge, a desk. "
                                       "A character crafts it when it stands within range.");
    type.field("station", &CraftingStation::station)
        .tooltip("The kind of station, as recipes name it.");
    type.field("range", &CraftingStation::range)
        .range(0.1, 20, 0.1)
        .tooltip("How close the maker must stand.");
    type.check([](const Entity &, const CraftingStation &craftingStation, const CheckContext &,
                  std::vector<std::string> &problems) {
        if (craftingStation.station.empty())
            problems.push_back("names no kind of station, so no recipe can use it");
    });
}
void CraftingStation::onStart(GameContext &context) {
    context.services().get<SpatialIndexService>().track(entity(), "station", range, false);
}
void CraftingStation::onDestroy(GameContext &context) {
    if (auto *spatial = context.services().find<SpatialIndexService>())
        spatial->untrack(entity().id(), "station");
}

// ---- Crafting
// ---------------------------------------------------------------------------------------
void Crafter::describe(TypeBuilder<Crafter> &type) {
    type.category("Items")
        .description(
            "Lets a character craft: it knows the project's default recipes and what it has "
            "learned, and crafts from its Inventory with its stats.")
        .dependsOn("Inventory");
    type.field("known", &Crafter::known)
        .ref("recipe")
        .tooltip("Recipes it has learned besides the default ones.");
    type.check([](const Entity &, const Crafter &crafter, const CheckContext &context,
                  std::vector<std::string> &problems) {
        if (!context.known)
            return;
        for (const std::string &id : crafter.known)
            if (!context.known("recipe", id))
                problems.push_back("knows the recipe '" + id + "', which is not defined");
    });
}

bool Crafter::knows(GameContext &context, const std::string &recipe) const {
    const RecipeDefinition *definition = gameData(context).recipes.recipes.find(recipe);
    return definition && (definition->knownByDefault || data::has(known, recipe));
}

bool Crafter::learn(GameContext &context, const std::string &recipe) {
    const RecipeDefinition *definition = gameData(context).recipes.recipes.find(recipe);
    if (!definition || knows(context, recipe))
        return false;
    known.push_back(recipe);
    Json data = Json::object();
    data.set("recipe", recipe);
    context.events().emit(GameEvent("craft.learned", entity().id(), {}, std::move(data)));
    return true;
}

bool Crafter::forget(const std::string &recipe) {
    const auto found = std::find(known.begin(), known.end(), recipe);
    if (found == known.end())
        return false;
    known.erase(found);
    return true;
}

std::vector<const RecipeDefinition *> Crafter::available(GameContext &context) const {
    std::vector<const RecipeDefinition *> list;
    for (const RecipeDefinition &recipe : gameData(context).recipes.recipes.all())
        if (recipe.knownByDefault || data::has(known, recipe.id))
            list.push_back(&recipe);
    return list;
}

namespace {
CraftResult refuse(std::string reason) {
    CraftResult result;
    result.reason = std::move(reason);
    return result;
}
std::string itemName(const ItemCatalog &items, const std::string &id) {
    const ItemDefinition *def = items.items.find(id);
    return def ? def->displayName : id;
}
} // namespace

CraftResult Crafter::check(GameContext &context, const std::string &recipeId) const {
    const GameData &data = gameData(context);
    const RecipeDefinition *recipe = data.recipes.recipes.find(recipeId);
    if (!recipe)
        return refuse("There is no such recipe.");
    if (!knows(context, recipeId))
        return refuse("You don't know how to make that.");
    const Inventory *inventory = entity().get<Inventory>();
    if (!inventory)
        return refuse("You have nothing to make it from.");
    if (!recipe->station.empty()) {
        bool found = false;
        if (const auto *spatial = context.services().find<SpatialIndexService>())
            for (const SpatialHash::Hit &hit :
                 spatial->near("station", entity().worldPosition(), 0.0F, levelOf(entity()))) {
                const Entity *place = context.scene().find(hit.id);
                const auto *station = place ? place->get<CraftingStation>() : nullptr;
                found = found || (station && station->station == recipe->station);
            }
        if (!found)
            return refuse("You need to be at a " + recipe->station + ".");
    }
    for (const auto &[stat, least] : recipe->statRequirements) {
        const auto *stats = entity().get<StatSet>();
        if (!stats || stats->value(stat) + 1e-9 < least) {
            const StatDefinition *definition = data.stats.stats.find(stat);
            return refuse("You need " + std::to_string(static_cast<int>(least)) + " " +
                          (definition ? definition->name : stat) + ".");
        }
    }
    if (!recipe->condition.empty()) {
        RuleContext rules(context);
        rules.self = entity().id();
        rules.actor = entity().id();
        rules.origin = "recipe '" + recipe->id + "'";
        if (!evaluate(recipe->condition, rules))
            return refuse("You can't make that right now.");
    }
    for (const RecipeIngredient &ingredient : recipe->ingredients) {
        const int have = !ingredient.item.empty()
                             ? inventory->count(ItemId(ingredient.item))
                             : inventory->countWithTag(context, ingredient.tag);
        if (have < ingredient.count)
            return refuse("You need " + std::to_string(ingredient.count) + " x " +
                          (!ingredient.item.empty() ? itemName(data.items, ingredient.item)
                                                    : "something " + ingredient.tag) +
                          ".");
        if (!ingredient.consume && !ingredient.item.empty() &&
            inventory->bestDurability(ItemId(ingredient.item)) == 0)
            return refuse("Your " + itemName(data.items, ingredient.item) + " is worn out.");
    }
    CraftResult ok;
    ok.ok = true;
    return ok;
}

CraftResult Crafter::craft(GameContext &context, const std::string &recipeId) {
    CraftResult result = check(context, recipeId);
    const GameData &data = gameData(context);
    Json info = Json::object();
    info.set("recipe", recipeId);
    if (!result.ok) {
        info.set("reason", result.reason);
        context.events().emit(GameEvent("craft.failed", entity().id(), {}, std::move(info)));
        return result;
    }
    const RecipeDefinition &recipe = *data.recipes.recipes.find(recipeId);
    Inventory &inventory = *entity().get<Inventory>();
    for (const RecipeIngredient &ingredient : recipe.ingredients) {
        if (ingredient.consume) {
            if (!ingredient.item.empty())
                inventory.remove(context, ItemId(ingredient.item), ingredient.count);
            else
                inventory.removeWithTag(context, ingredient.tag, ingredient.count);
        } else if (ingredient.wear > 0 && !ingredient.item.empty()) {
            const Inventory::Location where = inventory.find(ItemId(ingredient.item));
            if (where.valid())
                inventory.wear(context, where, ingredient.wear);
        }
    }
    Json made = Json::array();
    for (const RecipeOutput &output : recipe.outputs) {
        ItemStack stack = data.items.make(output.item, output.count);
        if (stack.empty())
            continue;
        stack.owner = inventory.owner;
        result.outputs.push_back(stack);
        Json line = Json::object();
        line.set("item", output.item);
        line.set("count", output.count);
        made.push(line);
        if (const int left = inventory.add(context, stack); left > 0) {
            stack.count = left;
            Pickup::place(context, stack, entity().worldPosition(), &entity());
        }
    }
    info.set("outputs", made);
    context.events().emit(GameEvent("craft.completed", entity().id(), {}, std::move(info)));
    return result;
}

Json Crafter::saveState() const {
    Json state = Json::object();
    state.set("known", data::toJsonList(known));
    return state;
}
Status Crafter::loadState(GameContext &context, const Json &state) {
    known = data::stringList(state, "known");
    known.erase(std::remove_if(known.begin(), known.end(),
                               [&](const std::string &id) {
                                   return !gameData(context).recipes.recipes.contains(id);
                               }),
                known.end());
    return success();
}

// ---- Rules
// ------------------------------------------------------------------------------------------
void registerCraftingRules(RuleCatalog &catalog) {
    using Kind = ParamSpec::Kind;
    const auto crafterOf = [](const Json &args, RuleContext &context) -> Crafter * {
        const auto found = context.entitiesFrom(args, "entity", "actor");
        return found.empty() ? nullptr : found.front()->get<Crafter>();
    };
    catalog.addPredicate({"KnowsRecipe",
                          "Items",
                          "True when the character knows how to make the recipe.",
                          {ParamSpec::make("recipe", Kind::Ref, true, "", "recipe"),
                           ParamSpec::make("entity", Kind::Entity, false, "default the actor")},
                          [crafterOf](const Json &args, RuleContext &context) {
                              const Crafter *crafter = crafterOf(args, context);
                              return crafter &&
                                     crafter->knows(context.game, args.get("recipe").asString());
                          },
                          nullptr});
    catalog.addPredicate(
        {"CanCraft",
         "Items",
         "True when the character could craft the recipe right now (ingredients, stats, station).",
         {ParamSpec::make("recipe", Kind::Ref, true, "", "recipe"),
          ParamSpec::make("entity", Kind::Entity, false, "default the actor")},
         [crafterOf](const Json &args, RuleContext &context) {
             const Crafter *crafter = crafterOf(args, context);
             return crafter && crafter->check(context.game, args.get("recipe").asString()).ok;
         },
         nullptr});
    catalog.addAction(
        {"Craft",
         "Items",
         "Has the character craft a recipe.",
         {ParamSpec::make("recipe", Kind::Ref, true, "", "recipe"),
          ParamSpec::make("entity", Kind::Entity, false, "default the actor")},
         [crafterOf](const Json &args, RuleContext &context) {
             Crafter *crafter = crafterOf(args, context);
             return crafter && crafter->craft(context.game, args.get("recipe").asString()).ok
                        ? ActionResult::Done
                        : ActionResult::Failed;
         },
         nullptr});
    catalog.addAction({"LearnRecipe",
                       "Items",
                       "Teaches the character a recipe.",
                       {ParamSpec::make("recipe", Kind::Ref, true, "", "recipe"),
                        ParamSpec::make("entity", Kind::Entity, false, "default the actor")},
                       [crafterOf](const Json &args, RuleContext &context) {
                           Crafter *crafter = crafterOf(args, context);
                           return crafter && crafter->learn(context.game,
                                                            args.get("recipe").asString())
                                      ? ActionResult::Done
                                      : ActionResult::Failed;
                       },
                       nullptr});
}
} // namespace yk
