#include "yk/runtime/Blackboard.hpp"
#include <cmath>
#include <cstdio>

namespace yk {
namespace {
std::string formatNumber(double value) {
    char buffer[48];
    if (value == std::trunc(value) && std::fabs(value) < 1e15)
        std::snprintf(buffer, sizeof buffer, "%.0f", value);
    else
        std::snprintf(buffer, sizeof buffer, "%.2f", value);
    return buffer;
}
} // namespace
void Blackboard::set(const std::string &key, double value) {
    values_[key] = value;
}
void Blackboard::set(const std::string &key, std::string value) {
    values_[key] = std::move(value);
}
void Blackboard::add(const std::string &key, double delta) {
    values_[key] = number(key) + delta;
}
double Blackboard::number(const std::string &key, double fallback) const {
    const auto found = values_.find(key);
    if (found == values_.end())
        return fallback;
    const double *value = std::get_if<double>(&found->second);
    return value ? *value : fallback;
}
std::string Blackboard::text(const std::string &key, std::string fallback) const {
    const auto found = values_.find(key);
    if (found == values_.end())
        return fallback;
    if (const auto *text = std::get_if<std::string>(&found->second))
        return *text;
    return formatNumber(std::get<double>(found->second));
}
std::string Blackboard::format(std::string_view templateText) const {
    std::string out;
    for (std::size_t i = 0; i < templateText.size(); ++i) {
        const char c = templateText[i];
        if ((c == '{' || c == '}') && i + 1 < templateText.size() && templateText[i + 1] == c) {
            out.push_back(c);
            ++i;
        } else if (c == '{') {
            const auto close = templateText.find('}', i + 1);
            if (close == std::string_view::npos) {
                out.push_back(c); // Unterminated: keep the text as written.
                continue;
            }
            out += text(std::string(templateText.substr(i + 1, close - i - 1)));
            i = close;
        } else {
            out.push_back(c);
        }
    }
    return out;
}
} // namespace yk
