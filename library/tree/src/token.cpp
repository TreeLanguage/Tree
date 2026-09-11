#include "token.hpp"
#include <string_view>

namespace tree {
std::string_view to_string(TokenType type) noexcept {
  switch (type) {
  case TokenType::Float:
    return "Float";
  case TokenType::String:
    return "String";
  case TokenType::Identifier:
    return "Identifier";
  case TokenType::LeftParen:
    return "LeftParen";
  case TokenType::RightParen:
    return "RightParen";
  case TokenType::Comma:
    return "Comma";
  case TokenType::Colon:
    return "Colon";
  case TokenType::Assign:
    return "Assign";
  case TokenType::Equal:
    return "Equal";
  case TokenType::LessThan:
    return "LessThan";
  case TokenType::Backslash:
    return "Backslash";
  case TokenType::Plus:
    return "Plus";
  case TokenType::Minus:
    return "Minus";
  case TokenType::Star:
    return "Star";
  case TokenType::Slash:
    return "Slash";
  case TokenType::Percent:
    return "Percent";
  case TokenType::Arrow:
    return "Arrow";
  case TokenType::Eof:
    return "Eof";
  default:
    return "Unknown";
  }
}
} // namespace tree