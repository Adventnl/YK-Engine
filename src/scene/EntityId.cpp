#include "yk/scene/EntityId.hpp"

namespace yk {
std::string toString(EntityId id) {
    if (!id)
        return {};
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i)
        out[static_cast<std::size_t>(i)] = digits[(id.value >> ((15 - i) * 4)) & 0xF];
    return out;
}
std::optional<EntityId> parseEntityId(std::string_view text) {
    if (text.empty())
        return EntityId{};
    if (text.size() != 16)
        return std::nullopt;
    std::uint64_t value = 0;
    for (const char c : text) {
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= static_cast<std::uint64_t>(c - 'a' + 10);
        else
            return std::nullopt;
    }
    if (value == 0)
        return std::nullopt; // "0000000000000000" is never produced; reject rather than alias null.
    return EntityId{value};
}
} // namespace yk
