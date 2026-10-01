#pragma once
#include "yk/core/Rng.hpp"
#include "yk/items/Items.hpp"
#include "yk/runtime/Services.hpp"
#include <set>

namespace yk {
class Entity;
class GameContext;

// Loot is data: tables that say what a container may hold (fixed things, weighted random things in
// amounts from a range, a guaranteed kind of thing, things that can only turn up once), and pools
// that say what must exist somewhere in the world and are dealt out at random among the containers
// of a group, so that what the scenario needs is always there while where it is changes.
struct IntRange {
    int min{1};
    int max{1};
    // A number, [low, high], or {"min": low, "max": high}.
    static Result<IntRange> fromJson(const Json &json, const char *what, int lowest = 0);
    Json toJson() const;
    friend bool operator==(const IntRange &, const IntRange &) = default;
};

struct LootEntry {
    enum class Kind { Item, Table, Category, Nothing };
    Kind kind{Kind::Item};
    std::string item;  // Item: what it is.
    std::string table; // Table: another table rolled in its place.
    std::string tag;   // Category: any item that has the tag, picked evenly.
    IntRange count;
    double weight{1.0};
    bool unique{false}; // Can be drawn once per roll, however often it comes up.

    static Result<LootEntry> fromJson(const Json &json);
    Json toJson() const;
};

struct LootTable {
    std::string id;
    std::string file;
    IntRange rolls; // How many times the entries are drawn from.
    std::vector<LootEntry> entries;
    std::vector<LootEntry> guaranteed; // Always included (weights ignored).

    static Result<LootTable> fromJson(const Json &json, std::vector<std::string> &warnings);
    Json toJson() const;
};

// What must exist, and among which containers it is dealt.
struct LootPool {
    std::string id;
    std::string file;
    std::vector<LootEntry>
        items;             // Each entry is dealt out as stacks of its count; weights are ignored.
    std::string group;     // The Container.group they are dealt among.
    int perContainer{0};   // At most this many of the pool's stacks in one container; 0: any.
    bool autoStart{false}; // Dealt when the scene starts.

    static Result<LootPool> fromJson(const Json &json, std::vector<std::string> &warnings);
    Json toJson() const;
};

using LootTableSet = DefinitionTable<LootTable>;
using LootPoolSet = DefinitionTable<LootPool>;

struct LootCatalog {
    LootTableSet tables;
    LootPoolSet pools;
    void load(const Json &document, const std::string &file, std::vector<DataProblem> &problems);
    void check(const ItemCatalog &items, std::vector<DataProblem> &problems) const;
};

// Rolls tables against a catalog with a generator of the caller's choosing (so a seed gives the
// same loot every time).
class LootRoller {
  public:
    LootRoller(const ItemCatalog &items, const LootCatalog &loot, Rng &rng)
        : items_(items), loot_(loot), rng_(rng) {}
    // The stacks one roll of the table gives, with equal stacks merged. Empty for an unknown table.
    std::vector<ItemStack> roll(std::string_view table);
    // The stacks a pool deals: each of its entries resolved to one or more stacks.
    std::vector<ItemStack> expand(const LootPool &pool);

  private:
    void rollInto(const LootTable &table, std::vector<ItemStack> &out, int depth);
    void resolve(const LootEntry &entry, std::vector<ItemStack> &out, int depth);
    const ItemCatalog &items_;
    const LootCatalog &loot_;
    Rng &rng_;
};

// Fills containers at the start of the game and deals pools among them.
class LootService final : public Service {
  public:
    const char *name() const override {
        return "loot";
    }
    void onStart(GameContext &context) override;
    void onShutdown(GameContext &context) override;

    // Rolls the container's table into its inventory (once). The seed, when not 0, makes the same
    // container hold the same things every game.
    void fill(GameContext &context, Entity &container);

    struct Placement {
        std::string pool;
        ItemStack stack;
        EntityId container; // Null when nothing had room.
    };
    // Deals the pool among the containers of its group (see LootPool).
    std::vector<Placement> scatter(GameContext &context, const std::string &pool);

  private:
    EventBus::Subscription subscription_{};
};

void registerLootRules(RuleCatalog &catalog);
} // namespace yk
