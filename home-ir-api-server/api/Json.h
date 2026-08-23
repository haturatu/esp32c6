#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <limits.h>

namespace HomeJson {

enum class ValueType : uint8_t { Null, Boolean, Integer, String, Object };

struct Field {
  String key;
  ValueType type = ValueType::Null;
  String stringValue;
  String objectValue;
  int32_t integerValue = 0;
  bool booleanValue = false;
};

class Object {
 public:
  static constexpr size_t kMaxFields = 16;

  bool parse(const String &input, String &error) {
    count_ = 0;
    input_ = &input;
    position_ = 0;
    error = "";
    skipWhitespace();
    if (!consume('{')) return fail(error, "object must start with {");
    skipWhitespace();
    if (consume('}')) {
      skipWhitespace();
      return atEnd();
    }

    while (true) {
      if (count_ >= kMaxFields) return fail(error, "too many fields");
      String key;
      if (!parseString(key)) return fail(error, "invalid object key");
      for (size_t i = 0; i < count_; i++) {
        if (values_[i].key == key) return fail(error, "duplicate object key");
      }
      skipWhitespace();
      if (!consume(':')) return fail(error, "missing colon");
      Field &field = values_[count_];
      field.key = key;
      if (!parseValue(field)) return fail(error, "invalid value");
      count_++;
      skipWhitespace();
      if (consume('}')) break;
      if (!consume(',')) return fail(error, "missing comma");
      skipWhitespace();
    }

    skipWhitespace();
    if (!atEnd()) return fail(error, "trailing data");
    return true;
  }

  size_t size() const { return count_; }
  const String &keyAt(const size_t index) const { return values_[index].key; }

  bool has(const char *key) const { return find(key) != nullptr; }

  bool getString(const char *key, String &value) const {
    const Field *field = find(key);
    if (field == nullptr || field->type != ValueType::String) return false;
    value = field->stringValue;
    return true;
  }

  bool getInteger(const char *key, int32_t &value) const {
    const Field *field = find(key);
    if (field == nullptr || field->type != ValueType::Integer) return false;
    value = field->integerValue;
    return true;
  }

  bool getBoolean(const char *key, bool &value) const {
    const Field *field = find(key);
    if (field == nullptr || field->type != ValueType::Boolean) return false;
    value = field->booleanValue;
    return true;
  }

  bool getObjectJson(const char *key, String &value) const {
    const Field *field = find(key);
    if (field == nullptr || field->type != ValueType::Object) return false;
    value = field->objectValue;
    return true;
  }

 private:
  const String *input_ = nullptr;
  size_t position_ = 0;
  Field values_[kMaxFields];
  size_t count_ = 0;

  bool atEnd() const { return input_ == nullptr || position_ >= input_->length(); }

  void skipWhitespace() {
    while (!atEnd()) {
      const char value = (*input_)[position_];
      if (value != ' ' && value != '\t' && value != '\r' && value != '\n') return;
      position_++;
    }
  }

  bool consume(const char expected) {
    skipWhitespace();
    if (atEnd() || (*input_)[position_] != expected) return false;
    position_++;
    return true;
  }

  bool fail(String &error, const char *message) {
    error = message;
    return false;
  }

  bool parseString(String &value) {
    skipWhitespace();
    if (atEnd() || (*input_)[position_++] != '"') return false;
    value = "";
    while (!atEnd()) {
      const char current = (*input_)[position_++];
      if (current == '"') return true;
      if (static_cast<uint8_t>(current) < 0x20) return false;
      if (current != '\\') {
        value += current;
        continue;
      }
      if (atEnd()) return false;
      const char escaped = (*input_)[position_++];
      switch (escaped) {
        case '"': value += '"'; break;
        case '\\': value += '\\'; break;
        case '/': value += '/'; break;
        case 'b': value += '\b'; break;
        case 'f': value += '\f'; break;
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        default: return false;
      }
    }
    return false;
  }

  bool parseValue(Field &field) {
    skipWhitespace();
    if (atEnd()) return false;
    const char current = (*input_)[position_];
    if (current == '"') {
      field.type = ValueType::String;
      return parseString(field.stringValue);
    }
    if (current == '{') {
      field.type = ValueType::Object;
      return parseObject(field.objectValue);
    }
    if (input_->startsWith("true", position_)) {
      position_ += 4;
      field.type = ValueType::Boolean;
      field.booleanValue = true;
      return true;
    }
    if (input_->startsWith("false", position_)) {
      position_ += 5;
      field.type = ValueType::Boolean;
      field.booleanValue = false;
      return true;
    }
    if (input_->startsWith("null", position_)) {
      position_ += 4;
      field.type = ValueType::Null;
      return true;
    }
    return parseInteger(field);
  }

  bool parseObject(String &value) {
    const size_t start = position_;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    while (!atEnd()) {
      const char current = (*input_)[position_++];
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (current == '\\') {
          escaped = true;
        } else if (current == '"') {
          inString = false;
        }
        continue;
      }
      if (current == '"') {
        inString = true;
      } else if (current == '{') {
        depth++;
      } else if (current == '}') {
        depth--;
        if (depth == 0) {
          value = input_->substring(start, position_);
          Object nested;
          String nestedError;
          return nested.parse(value, nestedError);
        }
      }
    }
    return false;
  }

  bool parseInteger(Field &field) {
    const size_t start = position_;
    bool negative = false;
    if (!atEnd() && (*input_)[position_] == '-') {
      negative = true;
      position_++;
    }
    const size_t digitStart = position_;
    if (!atEnd() && (*input_)[position_] == '0' && position_ + 1 < input_->length() &&
        (*input_)[position_ + 1] >= '0' && (*input_)[position_ + 1] <= '9') {
      position_ = start;
      return false;
    }
    int64_t value = 0;
    while (!atEnd()) {
      const char current = (*input_)[position_];
      if (current < '0' || current > '9') break;
      value = value * 10 + (current - '0');
      if (value > static_cast<int64_t>(INT32_MAX) + (negative ? 1 : 0)) return false;
      position_++;
    }
    if (position_ == digitStart) {
      position_ = start;
      return false;
    }
    if (!atEnd()) {
      const char next = (*input_)[position_];
      if (next == '.' || next == 'e' || next == 'E' ||
          (next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') ||
          next == '_') {
        position_ = start;
        return false;
      }
    }
    field.type = ValueType::Integer;
    if (negative && value == static_cast<int64_t>(INT32_MAX) + 1) {
      field.integerValue = INT32_MIN;
    } else {
      field.integerValue = negative ? -static_cast<int32_t>(value)
                                    : static_cast<int32_t>(value);
    }
    return true;
  }

  const Field *find(const char *key) const {
    for (size_t i = 0; i < count_; i++) {
      if (values_[i].key == key) return &values_[i];
    }
    return nullptr;
  }
};

inline void appendEscaped(String &output, const String &value) {
  output += '"';
  for (size_t i = 0; i < value.length(); i++) {
    const char current = value[i];
    switch (current) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default: output += current; break;
    }
  }
  output += '"';
}

inline String errorJson(const char *code, const String &message) {
  String body = "{\"ok\":false,\"error\":{\"code\":";
  appendEscaped(body, String(code));
  body += ",\"message\":";
  appendEscaped(body, message);
  body += "}}";
  return body;
}

}  // namespace HomeJson
