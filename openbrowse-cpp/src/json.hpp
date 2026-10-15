// Minimal dependency-free JSON used for OpenBrowse history persistence.
//
// Kept free of Windows headers so it can be unit tested on any host.
#pragma once

#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ob::json {

class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
 public:
  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool b) : type_(Type::Bool), bool_(b) {}
  Value(double n) : type_(Type::Number), number_(n) {}
  Value(int n) : type_(Type::Number), number_(n) {}
  Value(int64_t n) : type_(Type::Number), number_(static_cast<double>(n)) {}
  Value(std::string s) : type_(Type::String), string_(std::move(s)) {}
  Value(const char* s) : type_(Type::String), string_(s ? s : "") {}
  Value(Array a) : type_(Type::Array), array_(std::move(a)) {}
  Value(Object o) : type_(Type::Object), object_(std::move(o)) {}

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  bool asBool(bool fallback = false) const {
    if (type_ == Type::Bool) return bool_;
    if (type_ == Type::Number) return number_ != 0.0;
    return fallback;
  }

  double asNumber(double fallback = 0.0) const {
    return type_ == Type::Number ? number_ : fallback;
  }

  std::string asString(const std::string& fallback = std::string()) const {
    return type_ == Type::String ? string_ : fallback;
  }

  const Array& asArray() const {
    static const Array kEmpty;
    return type_ == Type::Array ? array_ : kEmpty;
  }

  const Object& asObject() const {
    static const Object kEmpty;
    return type_ == Type::Object ? object_ : kEmpty;
  }

  // Object member lookup; returns nullptr when absent.
  const Value* find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    auto it = object_.find(key);
    return it == object_.end() ? nullptr : &it->second;
  }

  std::string str(const std::string& key, const std::string& fallback = std::string()) const {
    const Value* v = find(key);
    return (v && v->isString()) ? v->string_ : fallback;
  }

  bool boolean(const std::string& key, bool fallback = false) const {
    const Value* v = find(key);
    return v ? v->asBool(fallback) : fallback;
  }

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  Array array_;
  Object object_;
};

namespace detail {

inline void escapeInto(const std::string& in, std::string* out) {
  out->push_back('"');
  for (unsigned char c : in) {
    switch (c) {
      case '"': *out += "\\\""; break;
      case '\\': *out += "\\\\"; break;
      case '\b': *out += "\\b"; break;
      case '\f': *out += "\\f"; break;
      case '\n': *out += "\\n"; break;
      case '\r': *out += "\\r"; break;
      case '\t': *out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          *out += buf;
        } else {
          out->push_back(static_cast<char>(c));
        }
    }
  }
  out->push_back('"');
}

class Parser {
 public:
  explicit Parser(const std::string& text) : text_(text) {}

  Value parse() {
    skipWhitespace();
    Value v = parseValue();
    skipWhitespace();
    if (pos_ != text_.size()) throw std::runtime_error("trailing data in JSON");
    return v;
  }

 private:
  [[noreturn]] void fail(const char* message) const {
    throw std::runtime_error(std::string("JSON parse error: ") + message);
  }

  void skipWhitespace() {
    while (pos_ < text_.size()) {
      char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

  void expect(char c) {
    if (peek() != c) fail("unexpected character");
    ++pos_;
  }

  bool literal(const char* word) {
    size_t len = std::char_traits<char>::length(word);
    if (text_.compare(pos_, len, word) != 0) return false;
    pos_ += len;
    return true;
  }

  Value parseValue() {
    switch (peek()) {
      case '{': return parseObject();
      case '[': return parseArray();
      case '"': return Value(parseString());
      case 't': if (literal("true")) return Value(true); fail("bad literal");
      case 'f': if (literal("false")) return Value(false); fail("bad literal");
      case 'n': if (literal("null")) return Value(); fail("bad literal");
      default: return parseNumber();
    }
  }

  Value parseObject() {
    expect('{');
    Object obj;
    skipWhitespace();
    if (peek() == '}') { ++pos_; return Value(std::move(obj)); }
    while (true) {
      skipWhitespace();
      std::string key = parseString();
      skipWhitespace();
      expect(':');
      skipWhitespace();
      obj.emplace(std::move(key), parseValue());
      skipWhitespace();
      if (peek() == ',') { ++pos_; continue; }
      expect('}');
      break;
    }
    return Value(std::move(obj));
  }

  Value parseArray() {
    expect('[');
    Array arr;
    skipWhitespace();
    if (peek() == ']') { ++pos_; return Value(std::move(arr)); }
    while (true) {
      skipWhitespace();
      arr.push_back(parseValue());
      skipWhitespace();
      if (peek() == ',') { ++pos_; continue; }
      expect(']');
      break;
    }
    return Value(std::move(arr));
  }

  void appendUtf8(unsigned int cp, std::string* out) const {
    if (cp <= 0x7F) {
      out->push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  unsigned int parseHex4() {
    if (pos_ + 4 > text_.size()) fail("truncated \\u escape");
    unsigned int value = 0;
    for (int i = 0; i < 4; ++i) {
      char c = text_[pos_++];
      value <<= 4;
      if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
      else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
      else fail("bad hex digit");
    }
    return value;
  }

  std::string parseString() {
    expect('"');
    std::string out;
    while (true) {
      if (pos_ >= text_.size()) fail("unterminated string");
      char c = text_[pos_++];
      if (c == '"') break;
      if (c != '\\') { out.push_back(c); continue; }
      if (pos_ >= text_.size()) fail("unterminated escape");
      char esc = text_[pos_++];
      switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          unsigned int cp = parseHex4();
          if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < text_.size() &&
              text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
            size_t save = pos_;
            pos_ += 2;
            unsigned int low = parseHex4();
            if (low >= 0xDC00 && low <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else {
              pos_ = save;
            }
          }
          appendUtf8(cp, &out);
          break;
        }
        default: fail("bad escape");
      }
    }
    return out;
  }

  Value parseNumber() {
    size_t start = pos_;
    if (peek() == '-' || peek() == '+') ++pos_;
    while (pos_ < text_.size()) {
      char c = text_[pos_];
      if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
        ++pos_;
      } else {
        break;
      }
    }
    if (start == pos_) fail("expected a value");
    return Value(std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr));
  }

  const std::string& text_;
  size_t pos_ = 0;
};

inline void serializeInto(const Value& value, std::string* out, int indent, int depth) {
  const bool pretty = indent > 0;
  const std::string pad = pretty ? std::string(static_cast<size_t>(indent * (depth + 1)), ' ') : std::string();
  const std::string padEnd = pretty ? std::string(static_cast<size_t>(indent * depth), ' ') : std::string();
  const char* nl = pretty ? "\n" : "";

  switch (value.type()) {
    case Type::Null: *out += "null"; break;
    case Type::Bool: *out += value.asBool() ? "true" : "false"; break;
    case Type::Number: {
      double n = value.asNumber();
      char buf[40];
      if (n == static_cast<long long>(n)) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
      } else {
        std::snprintf(buf, sizeof(buf), "%.17g", n);
      }
      *out += buf;
      break;
    }
    case Type::String: escapeInto(value.asString(), out); break;
    case Type::Array: {
      const Array& arr = value.asArray();
      if (arr.empty()) { *out += "[]"; break; }
      *out += "[";
      *out += nl;
      for (size_t i = 0; i < arr.size(); ++i) {
        *out += pad;
        serializeInto(arr[i], out, indent, depth + 1);
        if (i + 1 < arr.size()) *out += ",";
        *out += nl;
      }
      *out += padEnd;
      *out += "]";
      break;
    }
    case Type::Object: {
      const Object& obj = value.asObject();
      if (obj.empty()) { *out += "{}"; break; }
      *out += "{";
      *out += nl;
      size_t i = 0;
      for (const auto& kv : obj) {
        *out += pad;
        escapeInto(kv.first, out);
        *out += pretty ? ": " : ":";
        serializeInto(kv.second, out, indent, depth + 1);
        if (++i < obj.size()) *out += ",";
        *out += nl;
      }
      *out += padEnd;
      *out += "}";
      break;
    }
  }
}

}  // namespace detail

inline Value parse(const std::string& text) { return detail::Parser(text).parse(); }

inline std::string serialize(const Value& value, int indent = 0) {
  std::string out;
  detail::serializeInto(value, &out, indent, 0);
  return out;
}

}  // namespace ob::json