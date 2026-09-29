#pragma once
#include "yk/scene/Registry.hpp"
#include <string>

namespace yk {
// Markdown reference of every registered component (grouped by category) and entity template, taken
// straight from the reflection data the editor uses, so documentation cannot drift from the code.
std::string describeRegistryMarkdown(const ComponentRegistry &registry);
} // namespace yk
