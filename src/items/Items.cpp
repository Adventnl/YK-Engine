#include "yk/items/Items.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace yk {
std::string equipEffectId(const std::string &itemId) {
    return "equip:" + itemId;
}

double ItemDefinition::toolEfficiency(std::string_view action) const {
    const auto found = toolActions.find(std::string(action));
    return found == toolActions.end() ? 0.0 : found->second;
}

namespace {
Result<ItemWeapon> weaponFromJson(const Json &json) {
    if (!json.isObject())
        return Error{"'weapon' must be an object"};
    ItemWeapon weapon;
    constexpr double huge = 1.0e9;
    const auto read = [&](const char *key, double &out, double low, double high) -> Status {
        auto value = data::number(json, key, out, low, high);
        if (!value)
            return Error{"weapon: " + value.error()};
        out = value.value();
        return success();
    };
    using Field = std::tuple<const char *, double *, double, double>;
    const Field fields[] = {{"damage", &weapon.damage, 0.0, huge},
                            {"range", &weapon.range, 0.05, 100.0},
                            {"arc", &weapon.arc, 1.0, 360.0},
                            {"staminaCost", &weapon.staminaCost, 0.0, huge},
                            {"cooldown", &weapon.cooldown, 0.0, 3600.0},
                            {"knockback", &weapon.knockback, 0.0, huge},
                            {"chargeSeconds", &weapon.chargeSeconds, 0.0, 60.0},
                            {"chargeMultiplier", &weapon.chargeMultiplier, 1.0, 100.0}};
    for (const auto &[key, out, low, high] : fields)
        if (auto status = read(key, *out, low, high); !status)
            return Error{status.error()};
    weapon.damageType = data::optionalString(json, "damageType", weapon.damageType);
    auto cost = data::number(json, "durabilityCost", 1.0, 0.0, 100000.0);
    if (!cost)
        return Error{"weapon: " + cost.error()};
    weapon.durabilityCost = static_cast<int>(cost.value());
    return weapon;
}

Result<ItemEquip> equipFromJson(const Json &json) {
    if (!json.isObject())
        return Error{"'equip' must be an object"};
    ItemEquip equip;
    equip.slot = data::optionalString(json, "slot");
    if (equip.slot.empty())
        return Error{"equip: 'slot' is needed (the equipment slot it goes in)"};
    if (json.contains("modifiers")) {
        if (!json.get("modifiers").isArray())
            return Error{"equip: 'modifiers' must be a list"};
        for (std::size_t i = 0; i < json.get("modifiers").size(); ++i) {
            auto modifier = StatModifierSpec::fromJson(json.get("modifiers").at(i));
            if (!modifier)
                return Error{"equip: modifier " + std::to_string(i + 1) + ": " + modifier.error()};
            equip.modifiers.push_back(std::move(modifier.value()));
        }
    }
    equip.flags = data::stringList(json, "flags");
    equip.grants = data::stringList(json, "grants");
    equip.effects = data::stringList(json, "effects");
    if (json.contains("factors")) {
        const Json &factors = json.get("factors");
        if (!factors.isObject())
            return Error{
                "equip: 'factors' must be an object like {\"perception.visibility\": 0.5}"};
        for (std::size_t i = 0; i < factors.size(); ++i) {
            if (!factors.valueAt(i).isNumber() || factors.valueAt(i).asNumber() < 0.0)
                return Error{"equip: the factor '" + factors.keyAt(i) +
                             "' must be a number of 0 or more"};
            equip.factors[factors.keyAt(i)] = factors.valueAt(i).asNumber();
        }
    }
    if (json.contains("appearance")) {
        if (!json.get("appearance").isObject())
            return Error{"equip: 'appearance' must be an object of layer names and images"};
        equip.appearance = json.get("appearance");
    }
    return equip;
}

Result<ItemUse> useFromJson(const Json &json) {
    if (!json.isObject())
        return Error{"'use' must be an object"};
    ItemUse use;
    auto actions = Action::listFromJson(json.get("actions"));
    if (!actions)
        return Error{"use: " + actions.error()};
    use.actions = std::move(actions.value());
    auto condition = Condition::fromJson(json.get("requires"));
    if (!condition)
        return Error{"use: requires: " + condition.error()};
    use.condition = std::move(condition.value());
    use.consume = json.get("consume").asBool(false);
    auto cooldown = data::number(json, "cooldown", 0.0, 0.0, 86400.0);
    auto cost = data::number(json, "durabilityCost", 0.0, 0.0, 100000.0);
    if (!cooldown)
        return Error{"use: " + cooldown.error()};
    if (!cost)
        return Error{"use: " + cost.error()};
    use.cooldown = cooldown.value();
    use.durabilityCost = static_cast<int>(cost.value());
    if (use.actions.empty() && !use.consume && use.durabilityCost == 0)
        return Error{"use: it has no 'actions' and uses nothing up, so using it does nothing"};
    return use;
}
} // namespace

Result<ItemDefinition> ItemDefinition::fromJson(const Json &json,
                                                std::vector<std::string> &warnings) {
    if (!json.isObject())
        return Error{"an item must be an object"};
    ItemDefinition item;
    auto id = data::requiredString(json, "id");
    if (!id)
        return Error{id.error()};
    item.id = id.value();
    if (!data::validId(item.id))
        return Error{"'" + item.id + "' is not a usable id (letters, digits, '_' and '-')"};
    item.displayName = data::optionalString(json, "displayName", item.id);
    item.description = data::optionalString(json, "description");
    item.category = data::optionalString(json, "category");
    item.icon = data::optionalString(json, "icon");
    item.worldPrefab = data::optionalString(json, "worldPrefab");
    item.tags = data::stringList(json, "tags");
    item.grants = data::stringList(json, "grants");
    auto stack = data::number(json, "stackSize", 1.0, 1.0, 9999.0);
    auto durability = data::number(json, "durability", 0.0, 0.0, 1.0e6);
    auto value = data::number(json, "value", 0.0, 0.0, 1.0e9);
    auto weight = data::number(json, "weight", 0.0, 0.0, 1.0e6);
    if (!stack)
        return Error{stack.error()};
    if (!durability)
        return Error{durability.error()};
    if (!value)
        return Error{value.error()};
    if (!weight)
        return Error{weight.error()};
    item.stackSize = static_cast<int>(stack.value());
    item.durability = static_cast<int>(durability.value());
    item.value = static_cast<int>(value.value());
    item.weight = weight.value();
    if (item.durability > 0 && item.stackSize > 1)
        return Error{"it wears out ('durability') but stacks ('stackSize'): each worn item is its "
                     "own, so stackSize must be 1"};
    if (json.contains("toolActions")) {
        const Json &tools = json.get("toolActions");
        if (!tools.isObject())
            return Error{"'toolActions' must be an object like {\"Dig\": 12}"};
        for (std::size_t i = 0; i < tools.size(); ++i) {
            if (!tools.valueAt(i).isNumber() || !(tools.valueAt(i).asNumber() > 0.0))
                return Error{"the tool action '" + tools.keyAt(i) +
                             "' needs an efficiency above 0"};
            item.toolActions[tools.keyAt(i)] = tools.valueAt(i).asNumber();
        }
    }
    if (json.contains("weapon")) {
        auto weapon = weaponFromJson(json.get("weapon"));
        if (!weapon)
            return Error{weapon.error()};
        item.weapon = std::move(weapon.value());
    }
    if (json.contains("equip")) {
        auto equip = equipFromJson(json.get("equip"));
        if (!equip)
            return Error{equip.error()};
        item.equip = std::move(equip.value());
    }
    if (json.contains("use")) {
        auto use = useFromJson(json.get("use"));
        if (!use)
            return Error{use.error()};
        item.use = std::move(use.value());
    }
    item.data = json.get("data");
    data::warnUnknown(json,
                      {"id", "displayName", "description", "category", "icon", "worldPrefab",
                       "tags", "stackSize", "durability", "value", "weight", "toolActions",
                       "grants", "weapon", "equip", "use", "data"},
                      warnings);
    if (item.weapon && !json.get("weapon").isNull())
        data::warnUnknown(json.get("weapon"),
                          {"damage", "range", "arc", "staminaCost", "cooldown", "knockback",
                           "damageType", "chargeSeconds", "chargeMultiplier", "durabilityCost"},
                          warnings);
    return item;
}

Json ItemDefinition::toJson() const {
    Json json = Json::object();
    json.set("id", id);
    if (displayName != id)
        json.set("displayName", displayName);
    if (!description.empty())
        json.set("description", description);
    if (!category.empty())
        json.set("category", category);
    if (!icon.empty())
        json.set("icon", icon);
    if (!worldPrefab.empty())
        json.set("worldPrefab", worldPrefab);
    if (!tags.empty())
        json.set("tags", data::toJsonList(tags));
    if (stackSize != 1)
        json.set("stackSize", stackSize);
    if (durability != 0)
        json.set("durability", durability);
    if (value != 0)
        json.set("value", value);
    if (weight != 0.0)
        json.set("weight", weight);
    if (!toolActions.empty()) {
        Json tools = Json::object();
        for (const auto &[name, efficiency] : toolActions)
            tools.set(name, efficiency);
        json.set("toolActions", tools);
    }
    if (!grants.empty())
        json.set("grants", data::toJsonList(grants));
    if (weapon) {
        Json w = Json::object();
        w.set("damage", weapon->damage);
        w.set("range", weapon->range);
        w.set("arc", weapon->arc);
        if (weapon->staminaCost != 0.0)
            w.set("staminaCost", weapon->staminaCost);
        w.set("cooldown", weapon->cooldown);
        if (weapon->knockback != 0.0)
            w.set("knockback", weapon->knockback);
        w.set("damageType", weapon->damageType);
        if (weapon->chargeSeconds != 0.0) {
            w.set("chargeSeconds", weapon->chargeSeconds);
            w.set("chargeMultiplier", weapon->chargeMultiplier);
        }
        if (weapon->durabilityCost != 1)
            w.set("durabilityCost", weapon->durabilityCost);
        json.set("weapon", w);
    }
    if (equip) {
        Json e = Json::object();
        e.set("slot", equip->slot);
        if (!equip->modifiers.empty()) {
            Json list = Json::array();
            for (const StatModifierSpec &modifier : equip->modifiers)
                list.push(modifier.toJson());
            e.set("modifiers", list);
        }
        if (!equip->flags.empty())
            e.set("flags", data::toJsonList(equip->flags));
        if (!equip->factors.empty()) {
            Json factors = Json::object();
            for (const auto &[name, factor] : equip->factors)
                factors.set(name, factor);
            e.set("factors", factors);
        }
        if (!equip->grants.empty())
            e.set("grants", data::toJsonList(equip->grants));
        if (!equip->effects.empty())
            e.set("effects", data::toJsonList(equip->effects));
        if (equip->appearance.isObject())
            e.set("appearance", equip->appearance);
        json.set("equip", e);
    }
    if (use) {
        Json u = Json::object();
        if (!use->actions.empty()) {
            Json actions = Json::array();
            for (const Action &action : use->actions)
                actions.push(action.toJson());
            u.set("actions", actions);
        }
        if (!use->condition.empty())
            u.set("requires", use->condition.toJson());
        if (use->consume)
            u.set("consume", true);
        if (use->cooldown != 0.0)
            u.set("cooldown", use->cooldown);
        if (use->durabilityCost != 0)
            u.set("durabilityCost", use->durabilityCost);
        json.set("use", u);
    }
    if (!data.isNull())
        json.set("data", data);
    return json;
}

// ---- Stacks
// ---------------------------------------------------------------------------------------
Result<ItemStack> ItemStack::fromJson(const Json &json) {
    if (!json.isObject())
        return Error{
            "an item stack must be an object like {\"item\": \"screwdriver\", \"count\": 1}"};
    ItemStack stack;
    auto id = data::requiredString(json, "item");
    if (!id)
        return Error{id.error()};
    stack.item = ItemId(id.value());
    auto count = data::number(json, "count", 1.0, 1.0, 1.0e7);
    if (!count)
        return Error{count.error()};
    stack.count = static_cast<int>(count.value());
    auto durability = data::number(json, "durability", -1.0, -1.0, 1.0e7);
    if (!durability)
        return Error{durability.error()};
    stack.durability = static_cast<int>(durability.value());
    stack.owner = data::optionalString(json, "owner");
    stack.meta = json.get("meta");
    return stack;
}

Json ItemStack::toJson() const {
    Json json = Json::object();
    json.set("item", item.str());
    json.set("count", count);
    if (durability >= 0)
        json.set("durability", durability);
    if (!owner.empty())
        json.set("owner", owner);
    if (!meta.isNull())
        json.set("meta", meta);
    return json;
}

// ---- Catalog
// --------------------------------------------------------------------------------------
void ItemCatalog::load(const Json &document, const std::string &file,
                       std::vector<DataProblem> &problems) {
    if (document.contains("items"))
        items.load(document.get("items"), file, problems, "item");
}

void ItemCatalog::check(const StatCatalog &stats, std::vector<DataProblem> &problems) const {
    for (const ItemDefinition &item : items.all()) {
        const std::string where = "item '" + item.id + "': ";
        if (item.equip) {
            for (const StatModifierSpec &modifier : item.equip->modifiers)
                if (!stats.stats.contains(modifier.stat))
                    problems.push_back({item.file,
                                        where + "equipped, it changes the stat '" + modifier.stat +
                                            "', which is not defined",
                                        true});
            for (const std::string &effect : item.equip->effects)
                if (!stats.effects.contains(effect))
                    problems.push_back({item.file,
                                        where + "equipped, it applies the effect '" + effect +
                                            "', which is not defined",
                                        true});
        }
        if (item.weapon && item.weapon->staminaCost > 0.0 && !stats.stats.contains("stamina"))
            problems.push_back(
                {item.file,
                 where + "its weapon costs stamina but the project defines no 'stamina' stat",
                 false});
    }
}

void ItemCatalog::visitRules(const RuleSourceVisitor &visit) const {
    for (const ItemDefinition &item : items.all())
        if (item.use) {
            if (!item.use->actions.empty())
                visit({item.file, "item '" + item.id + "' use", nullptr, &item.use->actions});
            if (!item.use->condition.empty())
                visit({item.file, "item '" + item.id + "' use.requires", &item.use->condition,
                       nullptr});
        }
}

void ItemCatalog::visitAssets(
    const std::function<void(const std::string &, const std::string &, const std::string &,
                             const std::string &)> &visit) const {
    for (const ItemDefinition &item : items.all()) {
        const std::string label = "item '" + item.id + "'";
        if (!item.icon.empty())
            visit(item.file, label + " icon", item.icon, "texture");
        if (!item.worldPrefab.empty())
            visit(item.file, label + " worldPrefab", item.worldPrefab, "prefab");
        if (item.equip && item.equip->appearance.isObject())
            for (std::size_t i = 0; i < item.equip->appearance.size(); ++i)
                if (item.equip->appearance.valueAt(i).isString() &&
                    !item.equip->appearance.valueAt(i).asString().empty())
                    visit(item.file,
                          label + " appearance '" + item.equip->appearance.keyAt(i) + "'",
                          item.equip->appearance.valueAt(i).asString(), "texture");
    }
}

void ItemCatalog::synthesizeEffects(EffectTable &effects,
                                    std::vector<DataProblem> &problems) const {
    for (const ItemDefinition &item : items.all()) {
        if (!item.equip)
            continue;
        EffectDefinition effect;
        effect.id = equipEffectId(item.id);
        effect.file = item.file;
        effect.name = item.displayName;
        effect.modifiers = item.equip->modifiers;
        effect.flags = item.equip->flags;
        effect.factors = item.equip->factors;
        effect.grants = item.equip->grants;
        effect.tags = {"equipment"};
        effect.hidden = true;
        std::string error;
        if (!effects.add(std::move(effect), error))
            problems.push_back({item.file, "item '" + item.id + "': " + error, true});
    }
}

ItemStack ItemCatalog::make(std::string_view id, int count) const {
    const ItemDefinition *item = items.find(id);
    if (!item || count <= 0)
        return {};
    ItemStack stack;
    stack.item = item->itemId();
    stack.count = count;
    stack.durability = item->durability > 0 ? item->durability : -1;
    return stack;
}

std::vector<const ItemDefinition *> ItemCatalog::withTag(std::string_view tag) const {
    std::vector<const ItemDefinition *> found;
    for (const ItemDefinition &item : items.all())
        if (item.hasTag(tag))
            found.push_back(&item);
    return found;
}
} // namespace yk
