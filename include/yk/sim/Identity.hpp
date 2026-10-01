#pragma once
#include "yk/runtime/Services.hpp"
#include "yk/scene/Registry.hpp"
#include <map>
#include <string>
#include <vector>

// Who someone is, in a way that outlives the entity that stands for them. An EntityId belongs to
// one scene; a character who is saved, moves to another map, is named in a quest, is remembered by
// another character or is shown on another machine needs a name of its own. Identity gives a
// character a persistent id (authored in the scene: "npc.warden", "player.1"), a name to show, and
// the two words the rest of the simulation asks about, faction and role. Nothing here knows what a
// faction or a role is: schedules, jobs, access conditions and the AI use them as labels.
namespace yk {
class GameContext;
class RuleCatalog;
class StatusEffects;

class Identity final : public Component {
  public:
    // Persistent. Give every character that matters one; an empty id is made up from the entity's
    // name when the game starts, which is stable only while the name and the order of entities are.
    std::string id;
    std::string displayName;   // Empty: the entity's name.
    std::string faction;       // One of the project's factions (may be empty).
    std::string role;          // "guard", "inmate", "medic": the project's own words.
    Json data{Json::object()}; // Whatever else the game's rules want to read ({"home": "cell_12"}).
    static void describe(TypeBuilder<Identity> &type);

    void onStart(GameContext &context) override;
    void onDestroy(GameContext &context) override;
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;

    std::string name() const; // The name to show.
    // Where the character was when the game started: home, when no zone is named as home.
    Vec2 homePosition() const {
        return home_;
    }
    // Changes who they are with the rest of the world (the registry's indexes follow).
    void setFaction(GameContext &context, const std::string &value);
    void setRole(GameContext &context, const std::string &value);

  private:
    bool registered_{false};
    Vec2 home_{};
};

// The faction a character appears to belong to: their own, unless something they wear says
// otherwise (a status effect with the flag "disguise.<faction>").
std::string perceivedFaction(const Entity &subject);

// Finds characters by persistent id and by what they are. One per running game.
class ActorService final : public Service {
  public:
    const char *name() const override {
        return "actors";
    }
    std::string saveKey() const override {
        return "actors";
    }
    Json saveState() const override;
    Status loadState(GameContext &context, const Json &state) override;
    void describe(std::vector<std::pair<std::string, std::string>> &rows) const override;

    // The entity of a persistent id (null: nobody has it, or they are not in this scene).
    Entity *find(GameContext &context, std::string_view id) const;
    EntityId idOf(std::string_view persistentId) const;
    // Every registered character, in the order they started.
    const std::vector<EntityId> &all() const {
        return order_;
    }
    std::vector<Entity *> inFaction(GameContext &context, std::string_view faction) const;
    std::vector<Entity *> withRole(GameContext &context, std::string_view role) const;
    // Changes when anyone is added or removed or changes faction or role.
    std::uint64_t revision() const {
        return revision_;
    }

    // Used by Identity: registers a character under `wanted` (or a made-up id when it is empty or
    // taken) and says which id it got.
    std::string add(EntityId entity, const std::string &wanted, const std::string &entityName);
    void remove(EntityId entity, const std::string &persistentId);
    void touch() {
        ++revision_;
    }

  private:
    std::map<std::string, EntityId, std::less<>> byId_;
    std::vector<EntityId> order_;
    std::uint64_t serial_{0};
    std::uint64_t revision_{0};
};

void registerIdentityRules(RuleCatalog &catalog);
// Registers Identity, Relationships and the rules and facts around them.
void registerIdentityComponents(ComponentRegistry &registry);
} // namespace yk
