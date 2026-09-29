#include "yk/core/Json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace yk {
namespace {
constexpr int maximumDepth = 128;
const std::string emptyString;
const Json nullJson;
const std::vector<Json> emptyItems;

void appendUtf8(std::string &out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

void writeString(std::string &out, const std::string &text) {
    out.push_back('"');
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                out += buffer;
            } else {
                out.push_back(raw);
            }
        }
    }
    out.push_back('"');
}

// Shortest decimal that reads back as the same value (as float when `single`).
std::string formatNumber(double value, bool single) {
    if (!std::isfinite(value))
        return "null"; // JSON has no representation for NaN or infinity.
    if (value == std::trunc(value) && std::fabs(value) < 1e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.0f", value == 0.0 ? 0.0 : value); // No "-0".
        return buffer;
    }
    char buffer[40];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, value);
        const double back = std::strtod(buffer, nullptr);
        if (single ? static_cast<float>(back) == static_cast<float>(value) : back == value)
            break;
    }
    std::string text = buffer;
    for (char &c : text) // Defensive: a host that changed the C locale must not emit "1,5".
        if (c == ',')
            c = '.';
    return text;
}

class Parser {
  public:
    explicit Parser(std::string_view text) : text_(text) {}

    Result<Json> parse() {
        skipWhitespace();
        auto value = parseValue(0);
        if (!value)
            return value;
        skipWhitespace();
        if (position_ != text_.size())
            return fail("unexpected trailing characters");
        return value;
    }

  private:
    Error fail(const std::string &message) const {
        int line = 1, column = 1;
        for (std::size_t i = 0; i < position_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        return Error{"JSON parse error at line " + std::to_string(line) + ", column " +
                     std::to_string(column) + ": " + message};
    }
    bool atEnd() const {
        return position_ >= text_.size();
    }
    char peek() const {
        return text_[position_];
    }
    void skipWhitespace() {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r'))
            ++position_;
    }
    bool consume(std::string_view literal) {
        if (text_.substr(position_, literal.size()) != literal)
            return false;
        position_ += literal.size();
        return true;
    }

    Result<Json> parseValue(int depth) {
        if (depth > maximumDepth)
            return fail("nesting is too deep");
        if (atEnd())
            return fail("unexpected end of input");
        switch (peek()) {
        case '{':
            return parseObject(depth);
        case '[':
            return parseArray(depth);
        case '"': {
            auto text = parseString();
            if (!text)
                return Error{text.error()};
            return Json(std::move(text.value()));
        }
        case 't':
            return consume("true") ? Result<Json>(Json(true)) : fail("invalid literal");
        case 'f':
            return consume("false") ? Result<Json>(Json(false)) : fail("invalid literal");
        case 'n':
            return consume("null") ? Result<Json>(Json()) : fail("invalid literal");
        default:
            return parseNumber();
        }
    }

    Result<Json> parseNumber() {
        const std::size_t start = position_;
        if (!atEnd() && peek() == '-')
            ++position_;
        if (atEnd() || peek() < '0' || peek() > '9')
            return fail("invalid number");
        if (peek() == '0') {
            ++position_;
        } else {
            while (!atEnd() && peek() >= '0' && peek() <= '9')
                ++position_;
        }
        if (!atEnd() && peek() == '.') {
            ++position_;
            if (atEnd() || peek() < '0' || peek() > '9')
                return fail("digits required after decimal point");
            while (!atEnd() && peek() >= '0' && peek() <= '9')
                ++position_;
        }
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            ++position_;
            if (!atEnd() && (peek() == '+' || peek() == '-'))
                ++position_;
            if (atEnd() || peek() < '0' || peek() > '9')
                return fail("digits required in exponent");
            while (!atEnd() && peek() >= '0' && peek() <= '9')
                ++position_;
        }
        const std::string token(text_.substr(start, position_ - start));
        const double value = std::strtod(token.c_str(), nullptr);
        if (!std::isfinite(value))
            return fail("number out of range");
        return Json(value);
    }

    Result<std::uint32_t> parseHex4() {
        if (position_ + 4 > text_.size())
            return fail("truncated \\u escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else
                return fail("invalid \\u escape");
        }
        return value;
    }

    Result<std::string> parseString() {
        ++position_; // Opening quote.
        std::string out;
        while (true) {
            if (atEnd())
                return fail("unterminated string");
            const char c = text_[position_++];
            if (c == '"')
                return out;
            if (static_cast<unsigned char>(c) < 0x20)
                return fail("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (atEnd())
                return fail("unterminated escape");
            switch (const char escape = text_[position_++]; escape) {
            case '"':
            case '\\':
            case '/':
                out.push_back(escape);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                auto first = parseHex4();
                if (!first)
                    return Error{first.error()};
                std::uint32_t code = first.value();
                if (code >= 0xD800 && code <= 0xDBFF) {
                    if (!consume("\\u"))
                        return fail("high surrogate without low surrogate");
                    auto second = parseHex4();
                    if (!second)
                        return Error{second.error()};
                    if (second.value() < 0xDC00 || second.value() > 0xDFFF)
                        return fail("invalid low surrogate");
                    code = 0x10000 + ((code - 0xD800) << 10) + (second.value() - 0xDC00);
                } else if (code >= 0xDC00 && code <= 0xDFFF) {
                    return fail("unexpected low surrogate");
                }
                appendUtf8(out, code);
                break;
            }
            default:
                return fail("invalid escape sequence");
            }
        }
    }

    Result<Json> parseArray(int depth) {
        ++position_; // '['
        Json array = Json::array();
        skipWhitespace();
        if (!atEnd() && peek() == ']') {
            ++position_;
            return array;
        }
        while (true) {
            skipWhitespace();
            auto element = parseValue(depth + 1);
            if (!element)
                return element;
            array.push(std::move(element.value()));
            skipWhitespace();
            if (atEnd())
                return fail("unterminated array");
            if (peek() == ',') {
                ++position_;
                continue;
            }
            if (peek() == ']') {
                ++position_;
                return array;
            }
            return fail("expected ',' or ']'");
        }
    }

    Result<Json> parseObject(int depth) {
        ++position_; // '{'
        Json object = Json::object();
        skipWhitespace();
        if (!atEnd() && peek() == '}') {
            ++position_;
            return object;
        }
        while (true) {
            skipWhitespace();
            if (atEnd() || peek() != '"')
                return fail("expected string key");
            auto key = parseString();
            if (!key)
                return Error{key.error()};
            skipWhitespace();
            if (atEnd() || peek() != ':')
                return fail("expected ':' after key");
            ++position_;
            skipWhitespace();
            auto value = parseValue(depth + 1);
            if (!value)
                return value;
            if (object.contains(key.value()))
                return fail("duplicate key '" + key.value() + "'");
            object.set(std::move(key.value()), std::move(value.value()));
            skipWhitespace();
            if (atEnd())
                return fail("unterminated object");
            if (peek() == ',') {
                ++position_;
                continue;
            }
            if (peek() == '}') {
                ++position_;
                return object;
            }
            return fail("expected ',' or '}'");
        }
    }

    std::string_view text_;
    std::size_t position_{};
};

bool isScalar(const Json &json) {
    return !json.isArray() && !json.isObject();
}
} // namespace

Json::Type Json::type() const {
    return static_cast<Type>(value_.index());
}
bool Json::asBool(bool fallback) const {
    const auto *value = std::get_if<bool>(&value_);
    return value ? *value : fallback;
}
double Json::asNumber(double fallback) const {
    const auto *value = std::get_if<double>(&value_);
    return value ? *value : fallback;
}
std::int64_t Json::asInt(std::int64_t fallback) const {
    const auto *value = std::get_if<double>(&value_);
    if (!value)
        return fallback;
    constexpr double limit = 9.2e18;
    return static_cast<std::int64_t>(std::llround(std::clamp(*value, -limit, limit)));
}
const std::string &Json::asString() const {
    const auto *value = std::get_if<std::string>(&value_);
    return value ? *value : emptyString;
}
std::size_t Json::size() const {
    if (const auto *array = std::get_if<std::vector<Json>>(&value_))
        return array->size();
    if (const auto *object = std::get_if<ObjectData>(&value_))
        return object->keys.size();
    return 0;
}
const Json &Json::at(std::size_t index) const {
    const auto *array = std::get_if<std::vector<Json>>(&value_);
    return array && index < array->size() ? (*array)[index] : nullJson;
}
Json &Json::at(std::size_t index) {
    auto *array = std::get_if<std::vector<Json>>(&value_);
    if (!array || index >= array->size())
        throw std::out_of_range("Json array index out of range");
    return (*array)[index];
}
Json &Json::push(Json value) {
    if (isNull())
        value_ = std::vector<Json>{};
    auto *array = std::get_if<std::vector<Json>>(&value_);
    if (!array)
        throw std::logic_error("Json::push on a non-array value");
    array->push_back(std::move(value));
    return array->back();
}
const std::vector<Json> &Json::items() const {
    const auto *array = std::get_if<std::vector<Json>>(&value_);
    return array ? *array : emptyItems;
}
const Json *Json::find(std::string_view key) const {
    const auto *object = std::get_if<ObjectData>(&value_);
    if (!object)
        return nullptr;
    for (std::size_t i = 0; i < object->keys.size(); ++i)
        if (object->keys[i] == key)
            return &object->values[i];
    return nullptr;
}
Json *Json::find(std::string_view key) {
    return const_cast<Json *>(std::as_const(*this).find(key));
}
const Json &Json::get(std::string_view key) const {
    const Json *found = find(key);
    return found ? *found : nullJson;
}
Json &Json::set(std::string key, Json value) {
    if (isNull())
        value_ = ObjectData{};
    auto *object = std::get_if<ObjectData>(&value_);
    if (!object)
        throw std::logic_error("Json::set on a non-object value");
    for (std::size_t i = 0; i < object->keys.size(); ++i) {
        if (object->keys[i] == key) {
            object->values[i] = std::move(value);
            return object->values[i];
        }
    }
    object->keys.push_back(std::move(key));
    object->values.push_back(std::move(value));
    return object->values.back();
}
bool Json::erase(std::string_view key) {
    auto *object = std::get_if<ObjectData>(&value_);
    if (!object)
        return false;
    for (std::size_t i = 0; i < object->keys.size(); ++i) {
        if (object->keys[i] == key) {
            object->keys.erase(object->keys.begin() + static_cast<std::ptrdiff_t>(i));
            object->values.erase(object->values.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}
const std::string &Json::keyAt(std::size_t index) const {
    const auto *object = std::get_if<ObjectData>(&value_);
    return object && index < object->keys.size() ? object->keys[index] : emptyString;
}
const Json &Json::valueAt(std::size_t index) const {
    const auto *object = std::get_if<ObjectData>(&value_);
    return object && index < object->values.size() ? object->values[index] : nullJson;
}

void Json::write(std::string &out, int indent, int depth) const {
    const bool pretty = indent >= 0;
    const auto newline = [&](int level) {
        out.push_back('\n');
        out.append(static_cast<std::size_t>(level * indent), ' ');
    };
    switch (type()) {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += std::get<bool>(value_) ? "true" : "false";
        break;
    case Type::Number:
        out += formatNumber(std::get<double>(value_), single_);
        break;
    case Type::String:
        writeString(out, std::get<std::string>(value_));
        break;
    case Type::Array: {
        const auto &items = std::get<std::vector<Json>>(value_);
        if (items.empty()) {
            out += "[]";
            break;
        }
        bool inlineArray = !pretty;
        if (pretty) {
            inlineArray = true;
            std::size_t width = 0;
            for (const auto &item : items) {
                if (!isScalar(item) || (width += item.dump().size() + 2) > 72) {
                    inlineArray = false;
                    break;
                }
            }
        }
        out.push_back('[');
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i != 0)
                out += inlineArray && pretty ? ", " : ",";
            if (pretty && !inlineArray)
                newline(depth + 1);
            items[i].write(out, indent, depth + 1);
        }
        if (pretty && !inlineArray)
            newline(depth);
        out.push_back(']');
        break;
    }
    case Type::Object: {
        const auto &object = std::get<ObjectData>(value_);
        if (object.keys.empty()) {
            out += "{}";
            break;
        }
        out.push_back('{');
        for (std::size_t i = 0; i < object.keys.size(); ++i) {
            if (i != 0)
                out.push_back(',');
            if (pretty)
                newline(depth + 1);
            writeString(out, object.keys[i]);
            out += pretty ? ": " : ":";
            object.values[i].write(out, indent, depth + 1);
        }
        if (pretty)
            newline(depth);
        out.push_back('}');
        break;
    }
    }
}
std::string Json::dump(int indent) const {
    std::string out;
    write(out, indent, 0);
    return out;
}
Result<Json> Json::parse(std::string_view text) {
    return Parser(text).parse();
}
bool operator==(const Json &a, const Json &b) {
    if (a.type() != b.type())
        return false;
    switch (a.type()) {
    case Json::Type::Null:
        return true;
    case Json::Type::Bool:
        return a.asBool() == b.asBool();
    case Json::Type::Number:
        return a.asNumber() == b.asNumber();
    case Json::Type::String:
        return a.asString() == b.asString();
    case Json::Type::Array:
        return a.items() == b.items();
    case Json::Type::Object:
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const Json *other = b.find(a.keyAt(i));
            if (!other || !(*other == a.valueAt(i)))
                return false;
        }
        return true;
    }
    return false;
}
} // namespace yk
