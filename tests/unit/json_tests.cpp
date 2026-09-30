#include "support/check.hpp"
#include "yk/core/Color.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Json.hpp"
#include <cmath>
#include <filesystem>
#include <limits>

using yk::Json;

namespace {
Json parseOk(const std::string &text) {
    auto parsed = Json::parse(text);
    CHECK(parsed);
    return parsed ? parsed.value() : Json();
}
bool parseFails(const std::string &text) {
    return !Json::parse(text);
}

void scalars() {
    CHECK(parseOk("null").isNull());
    CHECK(parseOk("true").asBool() && !parseOk("false").asBool(true));
    CHECK_NEAR(parseOk("-12.5e2").asNumber(), -1250.0);
    CHECK(parseOk("  \"hi\"  ").asString() == "hi");
    CHECK(parseOk("0").asInt() == 0 && parseOk("42").asInt() == 42 && parseOk("-7").asInt() == -7);
    CHECK(parseOk("2.6").asInt() == 3); // Rounds to nearest.
    CHECK(Json("text").asNumber(9) == 9 && Json(3).asString().empty());
}
void structure() {
    const Json doc = parseOk(R"({"a":[1,2,{"b":null}],"c":{"d":"e"},"f":true})");
    CHECK(doc.isObject() && doc.size() == 3);
    CHECK(doc.get("a").size() == 3 && doc.get("a").at(2).get("b").isNull());
    CHECK(doc.get("c").get("d").asString() == "e");
    CHECK(doc.contains("f") && !doc.contains("zzz"));
    CHECK(doc.get("missing").isNull() && doc.get("a").at(99).isNull());
    CHECK(doc.keyAt(0) == "a" && doc.keyAt(1) == "c" && doc.keyAt(2) == "f");
    CHECK(parseOk("[]").isArray() && parseOk("[]").size() == 0);
    CHECK(parseOk("{}").isObject() && parseOk("{}").size() == 0);
}
void mutation() {
    Json object = Json::object();
    object.set("z", 1);
    object.set("a", 2);
    object.set("z", 3); // Replaces in place, order unchanged.
    CHECK(object.dump() == R"({"z":3,"a":2})");
    CHECK(object.erase("z") && !object.erase("z"));
    CHECK(object.dump() == R"({"a":2})");
    Json array;
    array.push(1).isNumber();
    array.push("two");
    CHECK(array.dump() == R"([1,"two"])");
    Json implicitObject;
    implicitObject.set("k", true);
    CHECK(implicitObject.isObject());
    bool threw = false;
    try {
        Json(5).push(1);
    } catch (const std::logic_error &) {
        threw = true;
    }
    CHECK(threw);
}
void numbers() {
    CHECK(Json(1.0).dump() == "1" && Json(-0.0).dump() == "0" && Json(1234567).dump() == "1234567");
    CHECK(Json(0.1F).dump() == "0.1"); // Single precision prints shortest float text.
    CHECK(Json(0.1).dump() == "0.1");
    CHECK(Json(1.5F).dump() == "1.5" && Json(-2.25).dump() == "-2.25");
    const float awkward = 0.3F * 3.0F;
    const Json roundTrip = parseOk(Json(awkward).dump());
    CHECK(static_cast<float>(roundTrip.asNumber()) == awkward);
    const double precise = 1.0 / 3.0;
    CHECK(parseOk(Json(precise).dump()).asNumber() == precise);
    CHECK(Json(std::numeric_limits<double>::quiet_NaN()).dump() == "null");
    CHECK(Json(std::numeric_limits<double>::infinity()).dump() == "null");
    CHECK(Json(1e300).dump() != "null" && parseOk(Json(1e300).dump()).asNumber() == 1e300);
    CHECK(Json(true).dump() == "true" && Json(nullptr).dump() == "null");
}
void strings() {
    // The JSON texts are named first: MSVC's preprocessor cannot stringize a raw string literal
    // that contains backslashes, which CHECK does with everything inside it.
    const std::string escapes = R"("a\nb\t\"q\"\\ \/")";
    const std::string accent = R"("\u00e9")";
    const std::string pair = R"("\ud83d\ude00")";
    const std::string control = R"("tab\there\u0001")";
    const std::string loneHigh = R"("\ud83d")";
    const std::string loneLow = R"("\ude00")";
    const std::string shortHex = R"("\u12")";
    const std::string badEscape = R"("bad \x escape")";
    CHECK(parseOk(escapes).asString() == "a\nb\t\"q\"\\ /");
    CHECK(parseOk(accent).asString() == "\xC3\xA9");
    CHECK(parseOk(pair).asString() == "\xF0\x9F\x98\x80"); // Surrogate pair.
    CHECK(Json("tab\there\x01").dump() == control);
    const std::string utf8 = "caf\xC3\xA9 \xE2\x82\xAC";
    CHECK(parseOk(Json(utf8).dump()).asString() == utf8); // UTF-8 passes through untouched.
    CHECK(parseFails(loneHigh) && parseFails(loneLow) && parseFails(shortHex));
    CHECK(parseFails("\"line\nbreak\"") && parseFails(badEscape) && parseFails("\"open"));
}
void malformed() {
    for (const char *text :
         {"",        "   ",   "{",        "[1,",   "[1 2]", "{\"a\" 1}", "{\"a\":1,}",
          "[1,]",    "{1:2}", "tru",      "nulll", "01",    "1.",        ".5",
          "-",       "1e",    "+1",       "'x'",   "[1]x",  "{} {}",     "{\"a\":1,\"a\":2}",
          "\"\\u\"", "NaN",   "Infinity", "1e999"}) {
        CHECK(parseFails(text));
    }
    const auto error = Json::parse("{\n  \"a\": 1,\n  \"b\": ?\n}");
    CHECK(!error && error.error().find("line 3") != std::string::npos);
    std::string deep(200, '[');
    deep += std::string(200, ']');
    CHECK(parseFails(deep)); // Nesting limit prevents stack exhaustion.
    std::string shallow(100, '[');
    shallow += std::string(100, ']');
    CHECK(!parseFails(shallow));
}
void formatting() {
    Json doc = Json::object();
    doc.set("name", "plate");
    Json position = Json::array();
    position.push(1.5F);
    position.push(-2);
    doc.set("position", position);
    Json list = Json::array();
    Json item = Json::object();
    item.set("k", 1);
    list.push(item);
    doc.set("list", list);
    doc.set("empty", Json::object());
    const std::string pretty = doc.dump(2);
    CHECK(pretty.find("\"position\": [1.5, -2]") !=
          std::string::npos); // Short scalar array inline.
    CHECK(pretty.find("\"empty\": {}") != std::string::npos);
    CHECK(pretty.find("\n    {\n") != std::string::npos); // Objects in arrays are expanded.
    CHECK(parseOk(pretty) == doc); // Pretty and compact forms parse to equal documents.
    CHECK(parseOk(doc.dump()) == doc);
    CHECK(doc.dump(2) == parseOk(pretty).dump(2)); // Serialization is stable.
}
void equality() {
    CHECK(parseOk(R"({"a":1,"b":2})") == parseOk(R"({"b":2,"a":1})")); // Key order irrelevant.
    CHECK(!(parseOk("[1,2]") == parseOk("[2,1]")));
    CHECK(!(Json(1) == Json("1")) && !(Json() == Json(false)));
}
void colors() {
    CHECK(yk::parseColor("#ff8000").value() == yk::Color{255, 128, 0, 255});
    CHECK(yk::parseColor("#FF800080").value() == yk::Color{255, 128, 0, 128});
    CHECK(!yk::parseColor("ff8000") && !yk::parseColor("#ff80") && !yk::parseColor("#gg0000") &&
          !yk::parseColor("#ff8000ffaa") && !yk::parseColor(""));
    CHECK(yk::formatColor({1, 2, 254, 255}) == "#0102feff");
    CHECK(yk::parseColor(yk::formatColor({9, 99, 199, 33})).value() == yk::Color{9, 99, 199, 33});
    CHECK(yk::lerp({0, 0, 0, 255}, {200, 100, 50, 255}, 0.5F) == yk::Color{100, 50, 25, 255});
}
void files() {
    const auto directory = std::filesystem::temp_directory_path() / "yk-json-file-test";
    std::filesystem::remove_all(directory);
    const auto path = directory / "nested" / "data.json";
    CHECK(yk::writeTextFileAtomic(path, R"({"v":1})"));
    CHECK(yk::readTextFile(path).value() == R"({"v":1})");
    CHECK(yk::writeTextFileAtomic(path, R"({"v":22})")); // Replaces an existing file.
    CHECK(yk::readTextFile(path).value() == R"({"v":22})");
    CHECK(!std::filesystem::exists(path.string() + ".tmp"));
    CHECK(!yk::readTextFile(directory / "missing.json"));
    CHECK(yk::toPortablePath(std::filesystem::path("a") / "b" / "c.txt") == "a/b/c.txt");
    std::filesystem::remove_all(directory);
}
} // namespace

int main() {
    scalars();
    structure();
    mutation();
    numbers();
    strings();
    malformed();
    formatting();
    equality();
    colors();
    files();
    return yk::test::finish("json");
}
