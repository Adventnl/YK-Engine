#include "yk/core/Log.hpp"
#include "yk/data/GameData.hpp"
#include "yk/items/Appearance.hpp"
#include "yk/items/Crafting.hpp"
#include "yk/items/Inventory.hpp"
#include "yk/items/Loot.hpp"
#include "yk/rules/RuleSet.hpp"
#include "yk/runtime/GameContext.hpp"
#include <algorithm>

namespace yk {
namespace {
using Kind = ParamSpec::Kind;
ParamSpec param(const char *name, Kind kind, bool required = false, const char *description = "",
                const char *refKind = "") {
    return ParamSpec::make(name, kind, required, description, refKind);
}

std::optional<Value> inventoryFact(RuleContext &context, Entity *subject, std::string_view rest) {
    const auto *inventory = subject ? subject->get<Inventory>() : nullptr;
    if (!inventory)
        return std::nullopt;
    const auto split = [&](std::string_view prefix) -> std::optional<std::string> {
        return rest.starts_with(prefix)
                   ? std::optional<std::string>(std::string(rest.substr(prefix.size())))
                   : std::nullopt;
    };
    if (const auto item = split("count."))
        return Value{static_cast<std::int64_t>(inventory->count(ItemId(*item)))};
    if (const auto item = split("has."))
        return Value{inventory->count(ItemId(*item)) > 0};
    if (const auto tag = split("tag."))
        return Value{static_cast<std::int64_t>(inventory->countWithTag(context.game, *tag))};
    if (const auto slot = split("equipped.")) {
        const ItemStack *worn = inventory->equipped(*slot);
        return Value{worn ? worn->item.str() : std::string()};
    }
    if (const auto item = split("durability."))
        return Value{static_cast<std::int64_t>(inventory->bestDurability(ItemId(*item)))};
    if (const auto token = split("token."))
        return Value{holdsToken(context.game, *subject, *token)};
    if (rest == "free")
        return Value{static_cast<std::int64_t>(inventory->freeSlots())};
    return std::nullopt;
}

Inventory *inventoryOf(const Json &args, RuleContext &context, const char *key = "entity") {
    const auto found = context.entitiesFrom(args, key, "actor");
    return found.empty() ? nullptr : found.front()->get<Inventory>();
}
int countOf(const Json &args, RuleContext &context) {
    return std::max(1, static_cast<int>(toInt(context.argument(args.get("count")), 1)));
}
void checkCount(const Json &args, RuleReport &report, const char *name) {
    if (args.contains("count") && args.get("count").isNumber() &&
        args.get("count").asNumber() < 1.0)
        report.error(std::string("action '") + name + "': 'count' must be at least 1");
}
} // namespace

void registerItemRules(RuleCatalog &catalog) {
    catalog.addFacts("inventory", inventoryFact, true);

    catalog.addPredicate(
        {"HasItem",
         "Items",
         "True when the character carries (or wears) at least this many of the item.",
         {param("item", Kind::Ref, true, "", "item"), param("count", Kind::Int, false, "default 1"),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const Inventory *inventory = inventoryOf(args, context);
             return inventory &&
                    inventory->has(ItemId(args.get("item").asString()), countOf(args, context));
         },
         nullptr});
    catalog.addPredicate(
        {"HasItemTag",
         "Items",
         "True when the character carries at least this many items that have the tag.",
         {param("tag", Kind::String, true), param("count", Kind::Int, false, "default 1"),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const Inventory *inventory = inventoryOf(args, context);
             return inventory &&
                    inventory->countWithTag(context.game, args.get("tag").asString()) >=
                        countOf(args, context);
         },
         nullptr});
    catalog.addPredicate(
        {"HoldsToken",
         "Items",
         "True when the character holds the permission token (a key, a keycard, an effect).",
         {param("token", Kind::String, true),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const auto found = context.entitiesFrom(args, "entity", "actor");
             return !found.empty() &&
                    holdsToken(context.game, *found.front(), args.get("token").asString());
         },
         nullptr});
    catalog.addPredicate(
        {"ItemEquipped",
         "Items",
         "True when the item (or anything, for a slot) is worn or wielded.",
         {param("item", Kind::Ref, false, "", "item"), param("slot", Kind::String, false),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const Inventory *inventory = inventoryOf(args, context);
             if (!inventory)
                 return false;
             for (const std::string &name : inventory->equipmentSlots) {
                 if (args.contains("slot") && args.get("slot").asString() != name)
                     continue;
                 const ItemStack *worn = inventory->equipped(name);
                 if (worn &&
                     (!args.contains("item") || worn->item.str() == args.get("item").asString()))
                     return true;
             }
             return false;
         },
         [](const Json &args, RuleReport &report) {
             if (!args.contains("item") && !args.contains("slot"))
                 report.error("condition 'ItemEquipped' needs an 'item' or a 'slot'");
         }});
    catalog.addPredicate(
        {"HasRoomFor",
         "Items",
         "True when the item would fit in the character's inventory.",
         {param("item", Kind::Ref, true, "", "item"), param("count", Kind::Int, false, "default 1"),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const Inventory *inventory = inventoryOf(args, context);
             if (!inventory)
                 return false;
             const int count = countOf(args, context);
             return inventory->canAdd(
                        context.game,
                        gameData(context.game).items.make(args.get("item").asString(), count)) >=
                    count;
         },
         nullptr});

    catalog.addAction(
        {"GiveItem",
         "Items",
         "Puts items in the character's inventory; what does not fit lands on the floor.",
         {param("item", Kind::Ref, true, "", "item"), param("count", Kind::Int, false, "default 1"),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             bool done = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "actor"))
                 if (auto *inventory = entity->get<Inventory>()) {
                     ItemStack stack =
                         gameData(context.game)
                             .items.make(args.get("item").asString(), countOf(args, context));
                     if (stack.empty())
                         continue;
                     const int left = inventory->add(context.game, stack);
                     if (left > 0) {
                         stack.count = left;
                         Pickup::place(context.game, stack, entity->worldPosition(), entity);
                     }
                     done = true;
                 }
             return done ? ActionResult::Done : ActionResult::Failed;
         },
         [](const Json &args, RuleReport &report) { checkCount(args, report, "GiveItem"); }});
    catalog.addAction(
        {"RemoveItem",
         "Items",
         "Takes items from the character (carried first, then worn); fails when it had fewer.",
         {param("item", Kind::Ref, true, "", "item"), param("count", Kind::Int, false, "default 1"),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const int count = countOf(args, context);
             bool all = false;
             for (Entity *entity : context.entitiesFrom(args, "entity", "actor"))
                 if (auto *inventory = entity->get<Inventory>())
                     all = inventory->remove(context.game, ItemId(args.get("item").asString()),
                                             count) >= count ||
                           all;
             return all ? ActionResult::Done : ActionResult::Failed;
         },
         [](const Json &args, RuleReport &report) { checkCount(args, report, "RemoveItem"); }});
    catalog.addAction(
        {"WearItem",
         "Items",
         "Wears an item down (a tool used, a weapon swung); it is gone when it reaches 0.",
         {param("item", Kind::Ref, true, "", "item"),
          param("amount", Kind::Int, false, "default 1"),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             Inventory *inventory = inventoryOf(args, context);
             if (!inventory)
                 return ActionResult::Failed;
             const ItemId item(args.get("item").asString());
             const int amount =
                 std::max(1, static_cast<int>(toInt(context.argument(args.get("amount")), 1)));
             for (const std::string &name : inventory->equipmentSlots)
                 if (const ItemStack *worn = inventory->equipped(name);
                     worn && worn->item == item && worn->durability >= 0) {
                     Inventory::Location where;
                     where.equipSlot = name;
                     inventory->wear(context.game, where, amount);
                     return ActionResult::Done;
                 }
             for (int i = 0; i < inventory->size(); ++i)
                 if (inventory->slot(i).item == item && inventory->slot(i).durability >= 0) {
                     Inventory::Location where;
                     where.slot = i;
                     inventory->wear(context.game, where, amount);
                     return ActionResult::Done;
                 }
             return ActionResult::Failed;
         },
         nullptr});
    catalog.addAction({"EquipItem",
                       "Items",
                       "Wears or wields an item the character carries.",
                       {param("item", Kind::Ref, true, "", "item"),
                        param("entity", Kind::Entity, false, "default the actor")},
                       [](const Json &args, RuleContext &context) {
                           Inventory *inventory = inventoryOf(args, context);
                           if (!inventory)
                               return ActionResult::Failed;
                           for (int i = 0; i < inventory->size(); ++i)
                               if (inventory->slot(i).item.str() == args.get("item").asString())
                                   return inventory->equip(context.game, i) ? ActionResult::Done
                                                                            : ActionResult::Failed;
                           return ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction({"UnequipItem",
                       "Items",
                       "Takes what is in an equipment slot off.",
                       {param("slot", Kind::String, true),
                        param("entity", Kind::Entity, false, "default the actor")},
                       [](const Json &args, RuleContext &context) {
                           Inventory *inventory = inventoryOf(args, context);
                           return inventory && inventory->unequip(context.game,
                                                                  args.get("slot").asString())
                                      ? ActionResult::Done
                                      : ActionResult::Failed;
                       },
                       nullptr});
    catalog.addAction(
        {"UseItem",
         "Items",
         "Uses an item the character carries (its own actions run, it may be used up).",
         {param("item", Kind::Ref, true, "", "item"),
          param("entity", Kind::Entity, false, "default the actor"),
          param("on", Kind::Entity, false, "what it is used on; default the target")},
         [](const Json &args, RuleContext &context) {
             Inventory *inventory = inventoryOf(args, context);
             if (!inventory)
                 return ActionResult::Failed;
             const Entity *on =
                 context.resolveOne(args.contains("on") ? args.get("on").asString() : "target");
             for (int i = 0; i < inventory->size(); ++i)
                 if (inventory->slot(i).item.str() == args.get("item").asString())
                     return inventory->use(context.game, i, on ? on->id() : EntityId{})
                                ? ActionResult::Done
                                : ActionResult::Failed;
             return ActionResult::Failed;
         },
         nullptr});
    catalog.addAction(
        {"DropItem",
         "Items",
         "Puts items from the character's inventory on the ground where it stands.",
         {param("item", Kind::Ref, true, "", "item"), param("count", Kind::Int, false, "default 1"),
          param("entity", Kind::Entity, false, "default the actor")},
         [](const Json &args, RuleContext &context) {
             const auto found = context.entitiesFrom(args, "entity", "actor");
             Inventory *inventory = found.empty() ? nullptr : found.front()->get<Inventory>();
             if (!inventory)
                 return ActionResult::Failed;
             int left = countOf(args, context);
             bool any = false;
             for (int i = 0; i < inventory->size() && left > 0; ++i)
                 if (inventory->slot(i).item.str() == args.get("item").asString()) {
                     const int here = std::min(left, inventory->slot(i).count);
                     if (inventory->drop(context.game, i, here, found.front()->worldPosition())) {
                         left -= here;
                         any = true;
                     }
                 }
             return any ? ActionResult::Done : ActionResult::Failed;
         },
         [](const Json &args, RuleReport &report) { checkCount(args, report, "DropItem"); }});
    catalog.addAction({"PickUpItem",
                       "Items",
                       "Has the character pick up an item lying on the ground.",
                       {param("pickup", Kind::Entity, false, "default the target"),
                        param("entity", Kind::Entity, false, "default the actor")},
                       [](const Json &args, RuleContext &context) {
                           Entity *pickup = context.resolveOne(
                               args.contains("pickup") ? args.get("pickup").asString() : "target");
                           const auto collectors = context.entitiesFrom(args, "entity", "actor");
                           if (!pickup || collectors.empty() || !pickup->get<Pickup>())
                               return ActionResult::Failed;
                           return pickup->get<Pickup>()->collect(context.game, *collectors.front())
                                      ? ActionResult::Done
                                      : ActionResult::Failed;
                       },
                       nullptr});
    for (const bool lock : {true, false})
        catalog.addAction({lock ? "LockContainer" : "UnlockContainer",
                           "Items",
                           lock ? "Locks a container." : "Unlocks a container.",
                           {param("entity", Kind::Entity, false, "default self")},
                           [lock](const Json &args, RuleContext &context) {
                               bool done = false;
                               for (Entity *entity : context.entitiesFrom(args, "entity", "self"))
                                   if (auto *container = entity->get<Container>()) {
                                       container->setLocked(context.game, lock);
                                       done = true;
                                   }
                               return done ? ActionResult::Done : ActionResult::Failed;
                           },
                           nullptr});
}

void registerItemComponents(ComponentRegistry &registry) {
    registerItemRules(registry.extend<RuleCatalog>());
    registerLootRules(registry.extend<RuleCatalog>());
    registerCraftingRules(registry.extend<RuleCatalog>());
    registry.add<Inventory>("Inventory");
    registry.add<Container>("Container");
    registry.add<Pickup>("Pickup");
    registry.add<CraftingStation>("CraftingStation");
    registry.add<Crafter>("Crafter");
    registry.add<AppearanceLayers>("AppearanceLayers");
}
} // namespace yk
