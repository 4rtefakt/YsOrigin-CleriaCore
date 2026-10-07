// JSON value / parser / writer — see json.hpp.
#include "cleria/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>

namespace cleria::json {

const char* type_name(Type t) {
    switch (t) {
    case Type::Null: return "null";
    case Type::Bool: return "bool";
    case Type::Int: return "int";
    case Type::Double: return "double";
    case Type::String: return "string";
    case Type::Array: return "array";
    case Type::Object: return "object";
    }
    return "?";
}

// ---- Value ---------------------------------------------------------------------------------

namespace {
const Value& null_value() { static const Value v; return v; }
const Array& empty_array() { static const Array a; return a; }
const Object& empty_object() { static const Object o; return o; }
const std::string& empty_string() { static const std::string s; return s; }
}  // namespace

Value::Value(Array a) : type_(Type::Array), a_(std::make_unique<Array>(std::move(a))) {}
Value::Value(Object o) : type_(Type::Object), o_(std::make_unique<Object>(std::move(o))) {}
Value::~Value() = default;

Value::Value(const Value& o) : type_(o.type_), b_(o.b_), i_(o.i_), d_(o.d_), s_(o.s_) {
    if (o.a_) a_ = std::make_unique<Array>(*o.a_);
    if (o.o_) o_ = std::make_unique<Object>(*o.o_);
}

Value& Value::operator=(const Value& o) {
    if (this != &o) {
        Value copy(o);
        *this = std::move(copy);
    }
    return *this;
}

std::int64_t Value::as_int(std::int64_t def) const {
    auto v = get_int();
    return v ? *v : def;
}

std::optional<std::int64_t> Value::get_int() const {
    if (type_ == Type::Int) return i_;
    if (type_ == Type::Double && std::isfinite(d_) && d_ == std::floor(d_) &&
        d_ >= -9223372036854775808.0 && d_ < 9223372036854775808.0)
        return static_cast<std::int64_t>(d_);
    return std::nullopt;
}

double Value::as_double(double def) const {
    if (type_ == Type::Double) return d_;
    if (type_ == Type::Int) return static_cast<double>(i_);
    return def;
}

const std::string& Value::as_string() const { return type_ == Type::String ? s_ : empty_string(); }
std::string Value::as_string(std::string_view def) const {
    return type_ == Type::String ? s_ : std::string(def);
}

const Array& Value::as_array() const { return type_ == Type::Array ? *a_ : empty_array(); }
const Object& Value::as_object() const { return type_ == Type::Object ? *o_ : empty_object(); }

Array& Value::arr() {
    if (type_ != Type::Array) { *this = Value(Array{}); }
    return *a_;
}
Object& Value::obj() {
    if (type_ != Type::Object) { *this = Value(Object{}); }
    return *o_;
}

std::size_t Value::size() const {
    if (type_ == Type::Array) return a_->size();
    if (type_ == Type::Object) return o_->size();
    return 0;
}

const Value& Value::operator[](std::size_t i) const {
    if (type_ == Type::Array && i < a_->size()) return (*a_)[i];
    return null_value();
}
const Value& Value::operator[](std::string_view key) const {
    const Value* v = find(key);
    return v ? *v : null_value();
}
const Value* Value::find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    auto it = o_->find(key);
    return it == o_->end() ? nullptr : &it->second;
}

bool Value::operator==(const Value& o) const {
    if (type_ != o.type_) return false;
    switch (type_) {
    case Type::Null: return true;
    case Type::Bool: return b_ == o.b_;
    case Type::Int: return i_ == o.i_;
    case Type::Double: return d_ == o.d_;
    case Type::String: return s_ == o.s_;
    case Type::Array: return *a_ == *o.a_;
    case Type::Object: return *o_ == *o.o_;
    }
    return false;
}

// ---- UTF-8 ---------------------------------------------------------------------------------

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

namespace {

// Decodes one UTF-8 sequence at s[i]; returns its length (1..4) and the code point, or 0 when
// the bytes are not a valid, shortest-form, non-surrogate sequence.
int decode_utf8(std::string_view s, std::size_t i, std::uint32_t& cp) {
    const auto b = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char c = b(i);
    if (c < 0x80) { cp = c; return 1; }
    int n;
    std::uint32_t min;
    if ((c & 0xE0) == 0xC0) { n = 2; cp = c & 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; min = 0x10000; }
    else return 0;
    if (i + n > s.size()) return 0;
    for (int k = 1; k < n; ++k) {
        const unsigned char cc = b(i + k);
        if ((cc & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return n;
}

}  // namespace

bool valid_utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        std::uint32_t cp;
        const int n = decode_utf8(s, i, cp);
        if (!n) return false;
        i += n;
    }
    return true;
}

// ---- parser --------------------------------------------------------------------------------

namespace {

class Parser {
public:
    Parser(std::string_view t, const ParseOptions& o) : s_(t), opt_(o) {}

    std::optional<Value> run(ParseError* err) {
        if (s_.size() > opt_.max_size) {
            fail(0, "input is " + std::to_string(s_.size()) + " bytes, over the " +
                        std::to_string(opt_.max_size) + "-byte limit");
        } else {
            if (s_.substr(0, 3) == "\xEF\xBB\xBF") p_ = 3;  // UTF-8 BOM
            ws();
            Value v;
            if (value(v, 0)) {
                ws();
                if (p_ != s_.size()) fail(p_, "unexpected data after the JSON value");
                else return v;
            }
        }
        if (err) *err = err_;
        return std::nullopt;
    }

private:
    std::string_view s_;
    const ParseOptions& opt_;
    std::size_t p_ = 0;
    ParseError err_;
    bool failed_ = false;

    bool fail(std::size_t at, const std::string& what) {
        if (failed_) return false;
        failed_ = true;
        int line = 1, col = 1;
        for (std::size_t k = 0; k < at && k < s_.size(); ++k) {
            if (s_[k] == '\n') { ++line; col = 1; } else { ++col; }
        }
        err_.offset = at;
        err_.line = line;
        err_.column = col;
        err_.message = "line " + std::to_string(line) + ", column " + std::to_string(col) + ": " + what;
        return false;
    }

    std::string describe_here() const {
        if (p_ >= s_.size()) return "end of input";
        const unsigned char c = static_cast<unsigned char>(s_[p_]);
        char buf[32];
        if (c >= 0x20 && c < 0x7F) std::snprintf(buf, sizeof buf, "'%c'", c);
        else std::snprintf(buf, sizeof buf, "byte 0x%02X", c);
        return buf;
    }

    void ws() {
        while (p_ < s_.size()) {
            const char c = s_[p_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++p_;
            else break;
        }
    }

    bool literal(std::string_view word) {
        if (s_.substr(p_, word.size()) == word) { p_ += word.size(); return true; }
        return false;
    }

    bool value(Value& out, int depth) {
        if (p_ >= s_.size()) return fail(p_, "unexpected end of input, expected a value");
        const char c = s_[p_];
        switch (c) {
        case '{': return object(out, depth + 1);
        case '[': return array(out, depth + 1);
        case '"': {
            std::string str;
            if (!string(str)) return false;
            out = Value(std::move(str));
            return true;
        }
        case 't': if (literal("true")) { out = Value(true); return true; } break;
        case 'f': if (literal("false")) { out = Value(false); return true; } break;
        case 'n': if (literal("null")) { out = Value(); return true; } break;
        case 'N':
            if (opt_.allow_nan && literal("NaN")) {
                out = Value(std::numeric_limits<double>::quiet_NaN());
                return true;
            }
            break;
        case 'I':
            if (opt_.allow_nan && literal("Infinity")) {
                out = Value(std::numeric_limits<double>::infinity());
                return true;
            }
            break;
        default:
            if (c == '-' || (c >= '0' && c <= '9')) return number(out);
            break;
        }
        return fail(p_, "unexpected " + describe_here() + ", expected a value");
    }

    bool number(Value& out) {
        const std::size_t start = p_;
        if (s_[p_] == '-') {
            ++p_;
            if (opt_.allow_nan && literal("Infinity")) {
                out = Value(-std::numeric_limits<double>::infinity());
                return true;
            }
        }
        const auto digit = [&] { return p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9'; };
        if (!digit()) return fail(p_, "expected a digit, got " + describe_here());
        if (s_[p_] == '0') {
            ++p_;
            if (digit()) return fail(p_ - 1, "a number cannot have a leading zero");
        } else {
            while (digit()) ++p_;
        }
        bool integral = true;
        if (p_ < s_.size() && s_[p_] == '.') {
            integral = false;
            ++p_;
            if (!digit()) return fail(p_, "expected a digit after the decimal point");
            while (digit()) ++p_;
        }
        if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
            integral = false;
            ++p_;
            if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) ++p_;
            if (!digit()) return fail(p_, "expected a digit in the exponent");
            while (digit()) ++p_;
        }
        const char* b = s_.data() + start;
        const char* e = s_.data() + p_;
        if (integral) {
            std::int64_t i = 0;
            auto r = std::from_chars(b, e, i);
            if (r.ec == std::errc() && r.ptr == e) { out = Value(i); return true; }
            // out of int64 range: fall through to a double
        }
        double d = 0;
        auto r = std::from_chars(b, e, d);
        if (r.ec == std::errc::result_out_of_range) {
            // from_chars reports overflow and underflow alike; underflow is a harmless 0
            bool neg_exp = false;
            for (const char* q = b; q != e; ++q)
                if (*q == 'e' || *q == 'E') { neg_exp = q + 1 != e && q[1] == '-'; break; }
            if (!neg_exp) return fail(start, "number out of range");
            out = Value(*b == '-' ? -0.0 : 0.0);
            return true;
        }
        if (r.ec != std::errc() || r.ptr != e) return fail(start, "malformed number");
        out = Value(d);
        return true;
    }

    bool hex4(std::uint32_t& v) {
        if (p_ + 4 > s_.size()) return fail(p_, "truncated \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[p_ + k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return fail(p_ + k, "bad hex digit in \\u escape");
        }
        p_ += 4;
        return true;
    }

    bool string(std::string& out) {
        ++p_;  // opening quote
        while (true) {
            // copy a run of plain bytes at once
            std::size_t run = p_;
            while (run < s_.size()) {
                const unsigned char c = static_cast<unsigned char>(s_[run]);
                if (c == '"' || c == '\\' || c < 0x20 || c >= 0x80) break;
                ++run;
            }
            out.append(s_.data() + p_, run - p_);
            p_ = run;
            if (p_ >= s_.size()) return fail(p_, "unterminated string");
            const unsigned char c = static_cast<unsigned char>(s_[p_]);
            if (c == '"') { ++p_; return true; }
            if (c < 0x20) return fail(p_, "control character in a string (must be escaped)");
            if (c >= 0x80) {
                std::uint32_t cp;
                const int n = decode_utf8(s_, p_, cp);
                if (!n) return fail(p_, "invalid UTF-8 in a string");
                out.append(s_.data() + p_, n);
                p_ += n;
                continue;
            }
            // backslash
            ++p_;
            if (p_ >= s_.size()) return fail(p_, "unterminated escape");
            const char e = s_[p_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // a high surrogate: a low one must follow to make a pair
                    std::uint32_t lo = 0;
                    if (s_.substr(p_, 2) == "\\u") {
                        const std::size_t save = p_;
                        p_ += 2;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            p_ = save;       // not a pair: lone high surrogate, re-read the next escape
                            cp = 0xFFFD;
                        }
                    } else {
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    cp = 0xFFFD;             // lone low surrogate
                }
                append_utf8(out, cp);
                break;
            }
            default:
                return fail(p_ - 1, std::string("invalid escape \\") + (e >= 0x20 && e < 0x7F ? std::string(1, e) : "?"));
            }
        }
    }

    bool array(Value& out, int depth) {
        if (depth > opt_.max_depth)
            return fail(p_, "nesting deeper than " + std::to_string(opt_.max_depth) + " levels");
        ++p_;
        Array a;
        ws();
        if (p_ < s_.size() && s_[p_] == ']') { ++p_; out = Value(std::move(a)); return true; }
        while (true) {
            ws();
            Value v;
            if (!value(v, depth)) return false;
            a.push_back(std::move(v));
            ws();
            if (p_ >= s_.size()) return fail(p_, "unterminated array, expected ',' or ']'");
            if (s_[p_] == ',') {
                ++p_;
                ws();
                if (p_ < s_.size() && s_[p_] == ']') return fail(p_, "trailing comma in array");
                continue;
            }
            if (s_[p_] == ']') { ++p_; break; }
            return fail(p_, "expected ',' or ']' in array, got " + describe_here());
        }
        out = Value(std::move(a));
        return true;
    }

    bool object(Value& out, int depth) {
        if (depth > opt_.max_depth)
            return fail(p_, "nesting deeper than " + std::to_string(opt_.max_depth) + " levels");
        ++p_;
        Object o;
        ws();
        if (p_ < s_.size() && s_[p_] == '}') { ++p_; out = Value(std::move(o)); return true; }
        while (true) {
            ws();
            if (p_ >= s_.size() || s_[p_] != '"') {
                if (p_ < s_.size() && s_[p_] == '}') return fail(p_, "trailing comma in object");
                return fail(p_, "expected a string key in object, got " + describe_here());
            }
            std::string key;
            if (!string(key)) return false;
            ws();
            if (p_ >= s_.size() || s_[p_] != ':')
                return fail(p_, "expected ':' after an object key, got " + describe_here());
            ++p_;
            ws();
            Value v;
            if (!value(v, depth)) return false;
            o.insert_or_assign(std::move(key), std::move(v));
            ws();
            if (p_ >= s_.size()) return fail(p_, "unterminated object, expected ',' or '}'");
            if (s_[p_] == ',') { ++p_; continue; }
            if (s_[p_] == '}') { ++p_; break; }
            return fail(p_, "expected ',' or '}' in object, got " + describe_here());
        }
        out = Value(std::move(o));
        return true;
    }
};

}  // namespace

std::optional<Value> parse(std::string_view text, ParseError* err, const ParseOptions& opt) {
    Parser p(text, opt);
    return p.run(err);
}

// ---- writer --------------------------------------------------------------------------------

void write_string(std::string& out, std::string_view s, bool ascii) {
    static const char* hex = "0123456789abcdef";
    const auto u4 = [&](std::uint32_t v) {
        out += "\\u";
        out += hex[(v >> 12) & 15];
        out += hex[(v >> 8) & 15];
        out += hex[(v >> 4) & 15];
        out += hex[v & 15];
    };
    out += '"';
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7F) u4(c);
                else out += static_cast<char>(c);
            }
            ++i;
            continue;
        }
        std::uint32_t cp;
        const int n = decode_utf8(s, i, cp);
        if (!n) {               // invalid byte in our own data: write U+FFFD, never broken JSON
            if (ascii) u4(0xFFFD); else append_utf8(out, 0xFFFD);
            ++i;
            continue;
        }
        if (!ascii) {
            out.append(s.data() + i, n);
        } else if (cp < 0x10000) {
            u4(cp);
        } else {
            const std::uint32_t v = cp - 0x10000;
            u4(0xD800 + (v >> 10));
            u4(0xDC00 + (v & 0x3FF));
        }
        i += n;
    }
    out += '"';
}

namespace {

void write_double(std::string& out, double d, bool allow_nan) {
    if (!std::isfinite(d)) {
        if (!allow_nan) { out += "null"; return; }
        out += std::isnan(d) ? "NaN" : (d > 0 ? "Infinity" : "-Infinity");
        return;
    }
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, d);   // shortest round-trip form
    std::string_view t(buf, r.ptr - buf);
    out += t;
    // keep it a double on re-read: 3 -> 3.0 (1e+20 already has an exponent)
    if (t.find_first_of(".eE") == std::string_view::npos) out += ".0";
}

void write_value(std::string& out, const Value& v, const WriteOptions& opt, int level) {
    const auto newline = [&](int lv) {
        if (opt.indent < 0) return;
        out += '\n';
        out.append(static_cast<std::size_t>(lv) * opt.indent, ' ');
    };
    switch (v.type()) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += v.as_bool() ? "true" : "false"; break;
    case Type::Int: out += std::to_string(v.as_int()); break;
    case Type::Double: write_double(out, v.as_double(), opt.allow_nan); break;
    case Type::String: write_string(out, v.as_string(), opt.ascii); break;
    case Type::Array: {
        const Array& a = v.as_array();
        out += '[';
        if (a.empty()) { out += ']'; break; }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (i) out += ',';
            newline(level + 1);
            write_value(out, a[i], opt, level + 1);
        }
        newline(level);
        out += ']';
        break;
    }
    case Type::Object: {
        const Object& o = v.as_object();
        out += '{';
        if (o.empty()) { out += '}'; break; }
        bool first = true;
        for (const auto& [k, val] : o) {
            if (!first) out += ',';
            first = false;
            newline(level + 1);
            write_string(out, k, opt.ascii);
            out += opt.indent < 0 ? ":" : ": ";
            write_value(out, val, opt, level + 1);
        }
        newline(level);
        out += '}';
        break;
    }
    }
}

}  // namespace

std::string write(const Value& v, const WriteOptions& opt) {
    std::string out;
    write_value(out, v, opt, 0);
    return out;
}

}  // namespace cleria::json
