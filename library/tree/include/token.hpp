#pragma once

#include "span.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace tree {
enum class TokenType : uint8_t {
  Float,
  String,
  Identifier,
  LeftParen,
  RightParen,
  Comma,
  Dot,
  Colon,
  Assign,
  Equal,
  LessThan,
  Backslash,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Arrow,
  If,
  Then,
  Else,
  Eof
};

struct Token {
  TokenType type = {};
  Span span;

  double float_value = 0.0;
  std::string string_value;
};

std::string_view to_string(TokenType type) noexcept;
} // namespace tree