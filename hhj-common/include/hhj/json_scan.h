// json_scan.h — reader for one flat JSON object (pure C++, no heap, no recursion).
//
// Just enough JSON for the HHJ link protocol (see hhj-common/PROTOCOL.md):
//   {"key":"string","n":123,"neg":-4,"other":true}
// Everything is bounded: input length, number of keys, digits in a number.
// Nested objects and arrays are rejected. A failed parse leaves the object empty.
//
// Host tests: hhj-common/test/test_json_scan.cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace hhj {

class FlatJson {
 public:
  static constexpr size_t kMaxLen = 512;     // longest input accepted
  static constexpr size_t kMaxFields = 16;   // most keys accepted

  enum class Type : uint8_t {
    Other,    // float, huge integer, true, false, null: valid JSON we never use
    String,   // "..."
    Int       // -?digits, at most 18 digits (always fits int64)
  };

  // True if `s[0..n)` is exactly one flat JSON object (surrounding whitespace allowed).
  bool parse(const char* s, size_t n) {
    count_ = 0;
    if (s == nullptr || n > kMaxLen) return false;               // (n == 0 fails below: no '{')
    size_t i = 0;
    skip_ws(s, n, &i);
    if (i >= n || s[i] != '{') return false;
    i++;
    skip_ws(s, n, &i);
    if (i < n && s[i] == '}') {
      i++;
    } else {
      for (;;) {
        Field f{};
        if (!scan_key(s, n, &i, &f)) return fail();
        skip_ws(s, n, &i);
        if (i >= n || s[i] != ':') return fail();
        i++;
        skip_ws(s, n, &i);
        if (!scan_value(s, n, &i, &f)) return fail();
        if (find(f.key, f.key_len) != nullptr) return fail();   // duplicate key
        if (count_ == kMaxFields) return fail();
        fields_[count_++] = f;
        skip_ws(s, n, &i);
        if (i >= n) return fail();
        if (s[i] == ',') { i++; skip_ws(s, n, &i); continue; }
        if (s[i] == '}') { i++; break; }
        return fail();
      }
    }
    skip_ws(s, n, &i);
    if (i != n) return fail();                                   // trailing garbage
    return true;
  }

  size_t size() const { return count_; }

  bool has(const char* key) const { return find(key, strlen(key)) != nullptr; }

  // The value of `key` if it is an integer.
  bool get_int(const char* key, int64_t* out) const {
    const Field* f = find(key, strlen(key));
    if (f == nullptr || f->type != Type::Int) return false;
    *out = f->num;
    return true;
  }

  // The value of `key` if it is a string without escape sequences.
  // `*out` points into the buffer given to parse() and is NOT NUL-terminated.
  bool get_token(const char* key, const char** out, size_t* len) const {
    const Field* f = find(key, strlen(key));
    if (f == nullptr || f->type != Type::String || f->escaped) return false;
    *out = f->str;
    *len = f->str_len;
    return true;
  }

 private:
  struct Field {
    const char* key;
    size_t key_len;
    Type type;
    bool escaped;
    int64_t num;
    const char* str;
    size_t str_len;
  };

  bool fail() { count_ = 0; return false; }

  static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
  static bool is_digit(char c) { return c >= '0' && c <= '9'; }
  static void skip_ws(const char* s, size_t n, size_t* i) {
    while (*i < n && is_ws(s[*i])) (*i)++;
  }

  const Field* find(const char* key, size_t len) const {
    for (size_t k = 0; k < count_; k++) {
      if (fields_[k].key_len == len && memcmp(fields_[k].key, key, len) == 0) return &fields_[k];
    }
    return nullptr;
  }

  // "key" — no escapes, no control characters.
  static bool scan_key(const char* s, size_t n, size_t* i, Field* f) {
    if (*i >= n || s[*i] != '"') return false;
    const size_t start = *i + 1;
    size_t j = start;
    for (;; j++) {
      if (j >= n) return false;
      const unsigned char c = static_cast<unsigned char>(s[j]);
      if (c == '"') break;
      if (c < 0x20 || c == '\\') return false;
    }
    f->key = s + start;
    f->key_len = j - start;
    *i = j + 1;
    return true;
  }

  static bool scan_value(const char* s, size_t n, size_t* i, Field* f) {
    if (*i >= n) return false;
    const char c = s[*i];
    if (c == '"') return scan_string(s, n, i, f);
    if (c == '-' || is_digit(c)) return scan_number(s, n, i, f);
    // true / false / null: a field of Type::Other, which is what `*f` already is
    return match(s, n, i, "true") || match(s, n, i, "false") || match(s, n, i, "null");   // else: '{', '[', junk
  }

  static bool scan_string(const char* s, size_t n, size_t* i, Field* f) {
    const size_t start = *i + 1;
    size_t j = start;
    bool escaped = false;
    for (;;) {
      if (j >= n) return false;
      const unsigned char c = static_cast<unsigned char>(s[j]);
      if (c == '"') break;
      if (c < 0x20) return false;
      if (c == '\\') {                                           // skip the escaped character, whatever it is;
        escaped = true;                                          // running off the end is caught at the top
        j += 2;
        continue;
      }
      j++;
    }
    f->type = Type::String;
    f->escaped = escaped;
    f->str = s + start;
    f->str_len = j - start;
    *i = j + 1;
    return true;
  }

  static bool scan_number(const char* s, size_t n, size_t* i, Field* f) {
    size_t j = *i;
    const bool neg = s[j] == '-';
    if (neg) j++;
    if (j >= n || !is_digit(s[j])) return false;
    if (s[j] == '0' && j + 1 < n && is_digit(s[j + 1])) return false;   // leading zero
    uint64_t v = 0;
    size_t digits = 0;
    while (j < n && is_digit(s[j])) {
      if (digits < 18) v = v * 10u + static_cast<uint64_t>(s[j] - '0');
      digits++;
      j++;
    }
    bool is_float = false;
    if (j < n && s[j] == '.') {
      j++;
      if (j >= n || !is_digit(s[j])) return false;
      while (j < n && is_digit(s[j])) j++;
      is_float = true;
    }
    if (j < n && (s[j] == 'e' || s[j] == 'E')) {
      j++;
      if (j < n && (s[j] == '+' || s[j] == '-')) j++;
      if (j >= n || !is_digit(s[j])) return false;
      while (j < n && is_digit(s[j])) j++;
      is_float = true;
    }
    if (!is_float && digits <= 18) {                             // otherwise it stays Type::Other
      f->type = Type::Int;
      f->num = neg ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
    }
    *i = j;
    return true;
  }

  static bool match(const char* s, size_t n, size_t* i, const char* word) {
    const size_t len = strlen(word);
    if (n - *i < len || memcmp(s + *i, word, len) != 0) return false;
    *i += len;
    return true;
  }

  Field fields_[kMaxFields];
  size_t count_ = 0;
};

}  // namespace hhj
