// cleria-jsontest — the JSON value / parser / writer (src/cleria/json.hpp): literals, numbers
// (int64 vs double, ranges), strings (escapes, surrogate pairs, UTF-8 validation), nesting limits,
// error messages with positions, the writer (compact, pretty, ASCII), and round trips.
// Usage: cleria-jsontest [ignored]   (pure; touches no file)
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "cleria/json.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[json] %-84s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

static std::optional<json::Value> P(std::string_view s, json::ParseError* e = nullptr,
                                    const json::ParseOptions& o = {}) {
    return json::parse(s, e, o);
}
static bool bad(std::string_view s) { return !P(s).has_value(); }
// '%' -> backslash: keeps JSON backslash-u escapes out of the C++ source text.
static std::string E(const char* s) {
    std::string r(s);
    for (char& c : r)
        if (c == '%') c = static_cast<char>(92);
    return r;
}
// Read access goes through a const reference (a non-const v["k"] is the inserting builder form).
static const json::Value& C(const std::optional<json::Value>& o) { return *o; }
static std::string err_of(std::string_view s, const json::ParseOptions& o = {}) {
    json::ParseError e;
    auto v = json::parse(s, &e, o);
    return v ? std::string("<parsed>") : e.message;
}

int main() {
    // ---- literals / whitespace ----
    check(P("null")->is_null(), "null");
    check(P(" \t\r\n true \n")->as_bool() == true, "true with surrounding whitespace");
    check(P("false")->is_bool() && !P("false")->as_bool(true), "false");
    check(bad("nul") && bad("tru") && bad("True") && bad("NULL"), "malformed literals refused");
    check(bad("") && bad("   "), "empty input refused");
    check(bad("1 2") && bad("{} x") && bad("[]]"), "trailing data refused");
    check(P("\xEF\xBB\xBF{\"a\":1}").has_value(), "a leading UTF-8 BOM is skipped");

    // ---- numbers ----
    auto v = P("0");
    check(v->is_int() && v->as_int() == 0, "0 is an int");
    v = P("-42");
    check(v->is_int() && v->as_int() == -42, "-42 is an int");
    v = P("9223372036854775807");
    check(v->is_int() && v->as_int() == std::numeric_limits<std::int64_t>::max(), "int64 max stays exact");
    v = P("-9223372036854775808");
    check(v->is_int() && v->as_int() == std::numeric_limits<std::int64_t>::min(), "int64 min stays exact");
    v = P("9007199254740993");
    check(v->is_int() && v->as_int() == 9007199254740993LL, "2^53+1 stays exact (not rounded through a double)");
    v = P("9223372036854775808");
    check(v->is_double() && v->as_double() == 9223372036854775808.0, "past int64 -> double");
    v = P("1.5");
    check(v->is_double() && v->as_double() == 1.5, "1.5 is a double");
    v = P("1e3");
    check(v->is_double() && v->as_double() == 1000.0 && v->as_int() == 1000, "1e3 is a double, as_int 1000");
    v = P("-2.5E-3");
    check(v->is_double() && std::fabs(v->as_double() + 0.0025) < 1e-15, "-2.5E-3");
    v = P("1712345678.123");
    check(v->as_int(-1) == -1 && v->get_int() == std::nullopt, "a fractional double is not an int");
    v = P("1712345678.0");
    check(v->as_int() == 1712345678, "an integral double converts with as_int (AP unix times)");
    check(P("1e-400").has_value() && P("1e-400")->as_double() == 0.0, "underflow -> 0");
    check(bad("1e400"), "overflow refused");
    check(bad("01") && bad("-01") && bad("00"), "leading zeros refused");
    check(bad("1.") && bad(".5") && bad("1.e3") && bad("1e") && bad("1e+") && bad("-") && bad("+1"),
          "malformed numbers refused");
    check(bad("NaN") && bad("Infinity") && bad("-Infinity"), "NaN / Infinity refused by default");
    json::ParseOptions nan_ok;
    nan_ok.allow_nan = true;
    check(std::isnan(P("NaN", nullptr, nan_ok)->as_double()) &&
              P("[Infinity,-Infinity]", nullptr, nan_ok)->operator[](1).as_double() < 0 &&
              std::isinf(P("Infinity", nullptr, nan_ok)->as_double()),
          "NaN / Infinity accepted with allow_nan (Python's json emits them)");

    // ---- strings ----
    v = P(R"("a\"b\\c\/d\b\f\n\r\t")");
    check(v->as_string() == "a\"b\\c/d\b\f\n\r\t", "simple escapes");
    v = P(E("\"%u0041%u00e9%u20AC\""));
    check(v->as_string() == "A\xC3\xA9\xE2\x82\xAC", "\\u escapes to 1-, 2-, 3-byte UTF-8");
    v = P(E("\"%ud83d%ude00\""));
    check(v->as_string() == "\xF0\x9F\x98\x80", "surrogate pair -> 4-byte UTF-8 (U+1F600)");
    v = P(E("\"%uDBFF%uDFFF\""));
    check(v->as_string() == "\xF4\x8F\xBF\xBF", "highest pair -> U+10FFFF");
    v = P(R"("x\ud800y")");
    check(v->as_string() == "x\xEF\xBF\xBDy", "lone high surrogate -> U+FFFD");
    v = P(R"("\udc00")");
    check(v->as_string() == "\xEF\xBF\xBD", "lone low surrogate -> U+FFFD");
    v = P(R"("\ud800A")");
    check(v->as_string() == "\xEF\xBF\xBD" "A", "high surrogate followed by a non-surrogate escape");
    v = P("\"Fran\xC3\xA7ois \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x8E\xAE\"");
    check(v && v->as_string() == "Fran\xC3\xA7ois \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x8E\xAE",
          "raw UTF-8 passes through");
    check(bad("\"\xC3\""), "truncated UTF-8 refused");
    check(bad("\"\xC0\xAF\""), "overlong UTF-8 refused");
    check(bad("\"\xED\xA0\x80\""), "UTF-8-encoded surrogate refused");
    check(bad("\"\xF4\x90\x80\x80\""), "code point above U+10FFFF refused");
    check(bad("\"\xFF\""), "invalid lead byte refused");
    check(bad("\"a\nb\"") && bad(std::string_view("\"a\0b\"", 5)), "raw control characters refused");
    check(bad(R"("\x")") && bad(R"("\u12")") && bad(R"("\u12G4")") && bad("\"abc") && bad("\"\\"),
          "bad escapes / unterminated strings refused");

    // ---- containers ----
    v = P(R"({"cmd":"RoomInfo","games":["A","B"],"n":3,"nested":{"x":[1,[2,[3]]]}})");
    check(v && v->is_object() && v->size() == 4, "object with 4 members");
    check(C(v)["cmd"].as_string() == "RoomInfo", "member lookup");
    check(C(v)["games"][1].as_string() == "B", "array index");
    check(C(v)["nested"]["x"][1][1][0].as_int() == 3, "deep chained lookup");
    check(C(v)["missing"]["deeper"][7].is_null(), "missing keys / indices give null, never throw");
    check(C(v)["games"]["notakey"].is_null() && C(v)["cmd"][0].is_null(), "wrong-type access gives null");
    check(v->contains("n") && !v->contains("N") && v->find("zz") == nullptr, "contains / find");
    v = P(R"({"a":1,"a":2})");
    check(v && v->size() == 1 && C(v)["a"].as_int() == 2, "duplicate key: last wins");
    check(P("[]")->is_array() && P("[]")->size() == 0 && P("{}")->is_object(), "empty containers");
    check(P(" [ 1 , 2 ] ")->size() == 2 && P("{ \"k\" : [ ] }")->size() == 1, "whitespace inside containers");
    check(bad("[1,]") && bad("{\"a\":1,}"), "trailing commas refused");
    check(bad("[1 2]") && bad("{\"a\" 1}") && bad("{a:1}") && bad("{\"a\":}") && bad("[,1]"),
          "missing separators / unquoted keys refused");
    check(bad("[1,2") && bad("{\"a\":1") && bad("{"), "unterminated containers refused");

    // ---- nesting limit ----
    std::string deep(300, '[');
    deep += std::string(300, ']');
    check(bad(deep), "300 levels refused at the default limit (256)");
    std::string ok(200, '[');
    ok += std::string(200, ']');
    check(P(ok).has_value(), "200 levels accepted");
    json::ParseOptions shallow;
    shallow.max_depth = 2;
    check(P("[[1]]", nullptr, shallow).has_value() && !P("[[[1]]]", nullptr, shallow).has_value() &&
              !P("{\"a\":{\"b\":{}}}", nullptr, shallow).has_value(),
          "custom depth limit applies to arrays and objects");
    std::string huge(100000, '[');      // must fail cleanly, not overflow the stack
    check(bad(huge), "100000 opening brackets fail cleanly");
    json::ParseOptions small;
    small.max_size = 8;
    check(err_of("[1,2,3,4,5]", small).find("limit") != std::string::npos, "input size limit");

    // ---- error messages ----
    {
        json::ParseError e;
        auto r = json::parse("{\n  \"a\": 1,\n  \"b\" 2\n}", &e);
        check(!r && e.line == 3 && e.column == 7 && e.message.find("expected ':'") != std::string::npos,
              "error has line 3, column 7 and says what was expected");
        std::printf("       message: %s\n", e.message.c_str());
        check(err_of("[1,2,x]").find("unexpected 'x'") != std::string::npos, "error names the bad character");
        check(err_of("\"abc").find("unterminated string") != std::string::npos, "unterminated string message");
        check(err_of("[1] 2").find("after the JSON value") != std::string::npos, "trailing data message");
        check(err_of("\"\xFF\"").find("UTF-8") != std::string::npos, "invalid UTF-8 message");
        check(err_of(deep).find("nesting deeper than 256") != std::string::npos, "depth message");
    }

    // ---- building values ----
    json::Value o;
    o["cmd"] = "Connect";
    o["items_handling"] = 7;
    o["tags"].push_back("AP");
    o["tags"].push_back("DeathLink");
    o["version"]["major"] = 0;
    o["version"]["class"] = "Version";
    o["slot_data"] = true;
    o["password"] = json::Value();
    o["time"] = 1712345678.5;
    check(o.is_object() && o["tags"].size() == 2 && o["version"]["class"].as_string() == "Version",
          "build an object with operator[] / push_back");
    const std::string compact = json::write(o);
    check(compact == R"({"cmd":"Connect","items_handling":7,"password":null,"slot_data":true,"tags":["AP","DeathLink"],"time":1712345678.5,"version":{"class":"Version","major":0}})",
          "compact writer (sorted keys, no spaces)");
    std::printf("       %s\n", compact.c_str());
    check(*P(compact) == o, "round trip compact");
    json::WriteOptions pretty;
    pretty.indent = 2;
    const std::string pr = json::write(o, pretty);
    check(pr.find("\n  \"cmd\": \"Connect\",") != std::string::npos && *P(pr) == o, "pretty writer + round trip");
    check(json::write(json::Value::array()) == "[]" && json::write(json::Value::object(), pretty) == "{}",
          "empty containers written compactly");

    // ---- writer: numbers and strings ----
    check(json::write(json::Value(3.0)) == "3.0" && P("3.0")->is_double(), "an integral double keeps '.0'");
    check(json::write(json::Value(0.1)) == "0.1", "shortest round-trip double");
    check(json::write(json::Value(1e300)) == "1e+300" && P("1e+300")->as_double() == 1e300, "big double");
    check(json::write(json::Value(std::numeric_limits<double>::quiet_NaN())) == "null", "NaN written as null");
    json::WriteOptions wn;
    wn.allow_nan = true;
    check(json::write(json::Value(-std::numeric_limits<double>::infinity()), wn) == "-Infinity",
          "-Infinity with allow_nan");
    check(json::write(json::Value(std::numeric_limits<std::int64_t>::min())) == "-9223372036854775808", "int64 min");
    check(json::write(json::Value("q\"\\\n\x01\x7F")) == R"("q\"\\\n\u0001\u007f")", "string escapes on write");
    const std::string uni = "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    check(json::write(json::Value(uni)) == "\"" + uni + "\"", "UTF-8 written raw by default");
    json::WriteOptions wa;
    wa.ascii = true;
    const std::string asc = json::write(json::Value(uni), wa);
    check(asc == E("\"%u00e9%u20ac%ud83d%ude00\""), "ASCII mode: \\u and surrogate pairs");
    std::printf("       %s\n", asc.c_str());
    check(P(json::write(json::Value(uni), wa))->as_string() == uni, "ASCII round trip");
    check(json::write(json::Value(std::string("a\xFF" "b"))) == "\"a\xEF\xBF\xBD" "b\"", "invalid UTF-8 written as U+FFFD");

    // ---- equality / copies ----
    json::Value a = *P(R"({"x":[1,2,{"y":"z"}]})");
    json::Value b = a;
    b["x"].arr()[2]["y"] = "w";
    check(a["x"][2]["y"].as_string() == "z" && b["x"][2]["y"].as_string() == "w", "copies are deep");
    check(json::Value(1) != json::Value(1.0), "int 1 != double 1.0 (types differ)");
    json::Value arr = *P("[5,6]");
    check(arr[0].as_int() == 5 && arr[1].as_int() == 6 && arr.is_array(), "v[0] / v[1] on a non-const array read by index");
    check(json::valid_utf8("abc\xC3\xA9") && !json::valid_utf8("\xC3"), "valid_utf8");

    // ---- an AP-sized message ----
    std::string big = "[{\"cmd\":\"DataPackage\",\"data\":{\"games\":{\"G\":{\"item_name_to_id\":{";
    for (int i = 0; i < 50000; ++i) {
        if (i) big += ',';
        big += "\"Item " + std::to_string(i) + "\":" + std::to_string(1000 + i);
    }
    big += "},\"checksum\":\"abc\"}}}}]";
    v = P(big);
    check(v && C(v)[0]["data"]["games"]["G"]["item_name_to_id"].size() == 50000 &&
              C(v)[0]["data"]["games"]["G"]["item_name_to_id"]["Item 49999"].as_int() == 50999,
          "a 50000-entry data package parses");

    std::printf("[json] %s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
