#pragma once
#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace yk {
// Stable, persisted identity of an entity within a scene. Zero is the null id. Ids are random
// 64-bit values so independently authored scenes and branches merge without renumbering.
struct EntityId {
    std::uint64_t value{};
    explicit operator bool() const {
        return value != 0;
    }
    friend bool operator==(EntityId, EntityId) = default;
    friend auto operator<=>(EntityId, EntityId) = default;
};
// A reference to another entity, resolved through the scene at use time. Same type as EntityId;
// the alias documents intent in component definitions.
using EntityRef = EntityId;

// 16 lowercase hex digits, or an empty string for the null id.
std::string toString(EntityId id);
// Inverse of toString. Empty text is the null id; malformed text yields nullopt.
std::optional<EntityId> parseEntityId(std::string_view text);
} // namespace yk

template <> struct std::hash<yk::EntityId> {
    std::size_t operator()(yk::EntityId id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
