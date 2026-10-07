// A small JSON value tree, parser and writer (RFC 8259), CleriaCore's own: no third-party code.
// Written for the Archipelago client (build/shell_re/ARCHIPELAGO.md) and usable by anything else
// that needs JSON (sidecar saves, state exports, tool output).
//
// - Values: null, bool, int (int64), double, string (UTF-8), array, object.
// - Numbers: a literal without '.', 'e' or 'E' that fits int64 is an Int; anything else is a
//   Double (so 9007199254740993 stays exact, 1.5 and 1e3 are doubles, 1e400 is +inf -> an error).
//   as_int() also accepts an integral double (AP sends unix times as floats).
// - Strings: \uXXXX escapes incl. surrogate pairs are decoded to UTF-8; raw bytes are checked to
//   be valid UTF-8 (overlongs, surrogates and > U+10FFFF refused). A lone surrogate escape becomes
//   U+FFFD (Python can emit one; a bad player name must not drop the connection) [H].
// - Objects: keys are unique and kept SORTED (std::map); a duplicate key keeps the last value,
//   like Python's json. Writer output is therefore deterministic.
// - Limits: nesting depth (default 256) and the input size are bounded; errors carry the line,
//   column and byte offset ("line 3, column 14: expected ',' or '}' in object").
// - Extensions, both off unless asked: NaN / Infinity / -Infinity literals (Python's json emits
//   them), a leading UTF-8 BOM is always skipped.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cleria::json {

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value, std::less<>>;

enum class Type : std::uint8_t { Null, Bool, Int, Double, String, Array, Object };
const char* type_name(Type t);

class Value {
public:
    Value() = default;                                  // null
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), b_(b) {}
    Value(int i) : type_(Type::Int), i_(i) {}
    Value(unsigned i) : type_(Type::Int), i_(i) {}
    Value(long i) : type_(Type::Int), i_(i) {}
    Value(long long i) : type_(Type::Int), i_(i) {}
    Value(unsigned long i) : type_(Type::Int), i_(static_cast<std::int64_t>(i)) {}
    Value(unsigned long long i) : type_(Type::Int), i_(static_cast<std::int64_t>(i)) {}
    Value(double d) : type_(Type::Double), d_(d) {}
    Value(const char* s) : type_(Type::String), s_(s) {}
    Value(std::string s) : type_(Type::String), s_(std::move(s)) {}
    Value(std::string_view s) : type_(Type::String), s_(s) {}
    Value(Array a);
    Value(Object o);

    Value(const Value& o);
    Value(Value&& o) noexcept = default;
    Value& operator=(const Value& o);
    Value& operator=(Value&& o) noexcept = default;
    ~Value();

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_int() const { return type_ == Type::Int; }
    bool is_double() const { return type_ == Type::Double; }
    bool is_number() const { return type_ == Type::Int || type_ == Type::Double; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    // Lenient getters: the value, or `def` when the type does not fit. as_int takes an Int, or a
    // Double holding an integer in int64 range; as_double takes either number.
    bool as_bool(bool def = false) const { return is_bool() ? b_ : def; }
    std::int64_t as_int(std::int64_t def = 0) const;
    double as_double(double def = 0.0) const;
    const std::string& as_string() const;              // "" unless a string
    std::string as_string(std::string_view def) const;
    std::optional<std::int64_t> get_int() const;      // nullopt unless as_int would succeed

    // Containers. The const accessors never throw: a missing key / index, or the wrong type,
    // returns a shared null value. NOTE: on a non-const Value, v["key"] is the BUILDER form: it
    // inserts the key and turns a non-object into an object. Read through a const reference.
    const Array& as_array() const;                    // empty unless an array
    const Object& as_object() const;                  // empty unless an object
    Array& arr();                                     // makes this an array if it is not one
    Object& obj();                                    // makes this an object if it is not one
    std::size_t size() const;                         // elements / members, else 0
    const Value& operator[](std::size_t i) const;
    const Value& operator[](std::size_t i) { return static_cast<const Value&>(*this)[i]; }   // v[0]: an index, never a key
    const Value& operator[](std::string_view key) const;
    const Value* find(std::string_view key) const;    // nullptr when absent / not an object
    bool contains(std::string_view key) const { return find(key) != nullptr; }
    Value& operator[](const std::string& key) { return obj()[key]; }   // inserts
    template <std::size_t N>                                           // literals (not const char*,
    Value& operator[](const char (&key)[N]) { return obj()[key]; }    // so v[0] stays an index)
    void push_back(Value v) { arr().push_back(std::move(v)); }

    bool operator==(const Value& o) const;           // deep; Int 1 != Double 1.0
    bool operator!=(const Value& o) const { return !(*this == o); }

private:
    Type type_ = Type::Null;
    bool b_ = false;
    std::int64_t i_ = 0;
    double d_ = 0.0;
    std::string s_;
    std::unique_ptr<Array> a_;
    std::unique_ptr<Object> o_;
};

struct ParseOptions {
    int max_depth = 256;                       // arrays / objects nested deeper are an error
    std::size_t max_size = 64u << 20;          // bytes of input
    bool allow_nan = false;                    // accept NaN, Infinity, -Infinity
};

struct ParseError {
    std::string message;                       // "line L, column C: what"
    std::size_t offset = 0;                    // byte offset of the error
    int line = 1, column = 1;                  // 1-based; column counts bytes
};

// Parses one JSON text (surrounding whitespace allowed, nothing else after it).
std::optional<Value> parse(std::string_view text, ParseError* err = nullptr,
                           const ParseOptions& opt = {});

struct WriteOptions {
    int indent = -1;                           // < 0 compact; >= 0 pretty with that many spaces
    bool ascii = false;                        // escape every non-ASCII code point as \uXXXX
    bool allow_nan = false;                    // write NaN / Infinity; else non-finite -> null
};

std::string write(const Value& v, const WriteOptions& opt = {});
// Appends the JSON form of a string (quotes included).
void write_string(std::string& out, std::string_view s, bool ascii = false);

// Utilities shared with the writer / tests.
bool valid_utf8(std::string_view s);
void append_utf8(std::string& out, std::uint32_t cp);

}  // namespace cleria::json
