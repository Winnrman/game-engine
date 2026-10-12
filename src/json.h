#pragma once
// ============================================================================
// json.h : a small JSON reader, enough for data files like recipes.json.
// Also accepts // line comments, so data files can be annotated.
// ============================================================================
#include <string>
#include <vector>
#include <cstdlib>
#include <cstddef>
#include <algorithm>

namespace json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Value> array;         // Array elements, or Object values
    std::vector<std::string> keys;    // Object keys (same order as `array`)

    bool IsString() const { return type == String; }
    bool IsNumber() const { return type == Number; }
    bool IsArray()  const { return type == Array; }
    bool IsObject() const { return type == Object; }

    const Value* Find(const std::string& key) const {
        if (type != Object) return nullptr;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) return &array[i];
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(const std::string& text) : begin_(text.c_str()), p_(begin_), end_(begin_ + text.size()) {}

    bool Parse(Value& out, std::string& err) {
        if (!ParseValue(out, 0)) { err = error_; return false; }
        SkipSpace();
        if (p_ != end_) { Fail("unexpected text after the end"); err = error_; return false; }
        return true;
    }

private:
    const char* begin_;
    const char* p_;
    const char* end_;
    std::string error_;

    bool Fail(const std::string& what) {
        if (!error_.empty()) return false;
        int line = 1, col = 1;
        for (const char* c = begin_; c < p_ && c < end_; ++c) {
            if (*c == '\n') { ++line; col = 1; } else ++col;
        }
        error_ = "line " + std::to_string(line) + ", column " + std::to_string(col) + ": " + what;
        return false;
    }

    void SkipSpace() {
        while (p_ < end_) {
            if (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r') { ++p_; continue; }
            if (*p_ == '/' && p_ + 1 < end_ && p_[1] == '/') {
                while (p_ < end_ && *p_ != '\n') ++p_;
                continue;
            }
            break;
        }
    }

    bool Literal(const char* word) {
        const char* q = p_;
        for (const char* w = word; *w; ++w, ++q)
            if (q >= end_ || *q != *w) return false;
        p_ = q;
        return true;
    }

    bool ParseValue(Value& v, int depth) {
        if (depth > 64) return Fail("nested too deeply");
        SkipSpace();
        if (p_ >= end_) return Fail("unexpected end of file");
        char c = *p_;
        if (c == '{') return ParseObject(v, depth);
        if (c == '[') return ParseArray(v, depth);
        if (c == '"') { v.type = Value::String; return ParseString(v.string); }
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(v);
        if (Literal("true"))  { v.type = Value::Bool; v.boolean = true;  return true; }
        if (Literal("false")) { v.type = Value::Bool; v.boolean = false; return true; }
        if (Literal("null"))  { v.type = Value::Null; return true; }
        return Fail(std::string("unexpected character '") + c + "'");
    }

    bool ParseNumber(Value& v) {
        char* stop = nullptr;
        std::string tmp(p_, (size_t)std::min<ptrdiff_t>(end_ - p_, 64));
        v.number = std::strtod(tmp.c_str(), &stop);
        if (stop == tmp.c_str()) return Fail("bad number");
        p_ += stop - tmp.c_str();
        v.type = Value::Number;
        return true;
    }

    static void AppendUtf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    }

    bool ParseString(std::string& out) {
        ++p_;   // opening quote
        out.clear();
        while (p_ < end_ && *p_ != '"') {
            char c = *p_++;
            if (c == '\n') return Fail("line break inside a string");
            if (c != '\\') { out += c; continue; }
            if (p_ >= end_) break;
            char e = *p_++;
            switch (e) {
                case '"': out += '"'; break;   case '\\': out += '\\'; break;
                case '/': out += '/'; break;   case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;  case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;  case 't': out += '\t'; break;
                case 'u': {
                    if (end_ - p_ < 4) return Fail("bad \\u escape");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = *p_++;
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= h - '0';
                        else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                        else return Fail("bad \\u escape");
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return Fail(std::string("unknown escape \\") + e);
            }
        }
        if (p_ >= end_) return Fail("unterminated string");
        ++p_;   // closing quote
        return true;
    }

    bool ParseArray(Value& v, int depth) {
        v.type = Value::Array;
        ++p_;
        SkipSpace();
        if (p_ < end_ && *p_ == ']') { ++p_; return true; }
        for (;;) {
            v.array.emplace_back();
            if (!ParseValue(v.array.back(), depth + 1)) return false;
            SkipSpace();
            if (p_ < end_ && *p_ == ',') { ++p_; continue; }
            if (p_ < end_ && *p_ == ']') { ++p_; return true; }
            return Fail("expected ',' or ']' in array");
        }
    }

    bool ParseObject(Value& v, int depth) {
        v.type = Value::Object;
        ++p_;
        SkipSpace();
        if (p_ < end_ && *p_ == '}') { ++p_; return true; }
        for (;;) {
            SkipSpace();
            if (p_ >= end_ || *p_ != '"') return Fail("expected a \"key\" in object");
            v.keys.emplace_back();
            if (!ParseString(v.keys.back())) return false;
            SkipSpace();
            if (p_ >= end_ || *p_ != ':') return Fail("expected ':' after key");
            ++p_;
            v.array.emplace_back();
            if (!ParseValue(v.array.back(), depth + 1)) return false;
            SkipSpace();
            if (p_ < end_ && *p_ == ',') { ++p_; continue; }
            if (p_ < end_ && *p_ == '}') { ++p_; return true; }
            return Fail("expected ',' or '}' in object");
        }
    }
};

inline bool Parse(const std::string& text, Value& out, std::string& err) {
    return Parser(text).Parse(out, err);
}

} // namespace json
