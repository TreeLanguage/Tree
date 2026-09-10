#include "lexer.hpp"
#include "diagnostic.hpp"
#include "span.hpp"
#include "token.hpp"
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
const std::unordered_map<std::string_view, tree::TokenType> &keywords() {
  static const std::unordered_map<std::string_view, tree::TokenType> MAP = {
      {"if", tree::TokenType::If},
      {"then", tree::TokenType::Then},
      {"else", tree::TokenType::Else},
  };
  return MAP;
}

const std::unordered_map<std::string_view, tree::TokenType> &double_ops() {
  static const std::unordered_map<std::string_view, tree::TokenType> MAP = {
      {"==", tree::TokenType::Equal},
      {"->", tree::TokenType::Arrow},
  };
  return MAP;
}

constexpr std::array<std::pair<char, tree::TokenType>, 12> SINGLE_OPS = {{
    {'(', tree::TokenType::LeftParen},
    {')', tree::TokenType::RightParen},
    {',', tree::TokenType::Comma},
    {':', tree::TokenType::Colon},
    {'+', tree::TokenType::Plus},
    {'*', tree::TokenType::Star},
    {'/', tree::TokenType::Slash},
    {'%', tree::TokenType::Percent},
    {'=', tree::TokenType::Assign},
    {'<', tree::TokenType::LessThan},
    {'\\', tree::TokenType::Backslash},
    {'-', tree::TokenType::Minus},
}};

constexpr std::optional<tree::TokenType> lookup_single_op(char c) {
  for (const auto &[ch, tok] : SINGLE_OPS) {
    if (ch == c)
      return tok;
  }
  return std::nullopt;
}

class Lexer {
public:
  Lexer(std::string_view source, tree::DiagnosticEngine &diag)
      : source_(source), diag_(diag) {}

  std::vector<tree::Token> lexer() {
    std::vector<tree::Token> out;

    while (!at_end()) {
      skip_whitespace_and_comments();
      if (at_end()) {
        break;
      }

      const char c = peek();
      const int start_line = line_;
      const int start_col = col_;

      if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
        out.push_back(lex_number(start_line, start_col));
        continue;
      }

      if ((std::isalpha(static_cast<unsigned char>(c)) != 0) || c == '_') {
        out.push_back(lex_identifier_or_keyword(start_line, start_col));
        continue;
      }

      if (c == '"') {
        out.push_back(lex_string(start_line, start_col));
        continue;
      }

      tree::Token op_token;
      if (try_lex_operator(start_line, start_col, op_token)) {
        out.push_back(std::move(op_token));
        continue;
      }

      diag_.report(tree::Severity::Error,
                   tree::Span(start_line, start_col, start_line, start_col + 1),
                   std::string("unexpected character '") + c + "'");
      advance_pos(1);
    }

    out.push_back({.type = tree::TokenType::Eof,
                   .span = tree::Span(line_, col_, line_, col_),
                   .string_value = {}});
    return out;
  }

private:
  std::string_view source_;
  tree::DiagnosticEngine &diag_;
  size_t i_ = 0;
  int line_ = 1;
  int col_ = 1;

  [[nodiscard]] bool at_end() const { return i_ >= source_.size(); }
  [[nodiscard]] char peek() const {
    if (i_ >= source_.size()) {
      return '\0';
    }
    return source_[i_];
  }

  [[nodiscard]] char peek(size_t offset) const {
    const size_t idx = i_ + offset;
    if (idx >= source_.size()) {
      return '\0';
    }
    return source_[idx];
  }

  void advance_pos(size_t len, char ch = '\0') {
    if (ch == '\n') {
      line_++;
      col_ = 1;
    } else {
      col_ += static_cast<int>(len);
    }
    i_ += len;
  }

  void skip_whitespace_and_comments() {
    while (!at_end()) {
      const char c = peek();
      if (c == '\n') {
        advance_pos(1, '\n');
      } else if (std::isspace(static_cast<unsigned char>(c)) != 0) {
        advance_pos(1);
      } else if (c == '#') {
        while (!at_end() && peek() != '\n') {
          advance_pos(1);
        }
      } else {
        break;
      }
    }
  }

  tree::Token lex_number(int start_line, int start_col) {
    const size_t start_idx = i_;

    while (!at_end() &&
           (std::isdigit(static_cast<unsigned char>(peek())) != 0)) {
      advance_pos(1);
    }
    if (!at_end() && peek() == '.' && i_ + 1 < source_.size() &&
        (std::isdigit(static_cast<unsigned char>(peek(1))) != 0)) {
      advance_pos(1);
      while (!at_end() &&
             (std::isdigit(static_cast<unsigned char>(peek())) != 0)) {
        advance_pos(1);
      }
    }

    const std::string_view num_str = source_.substr(start_idx, i_ - start_idx);
    tree::Token t{.type = tree::TokenType::Float,
                  .span = tree::Span(start_line, start_col, line_, col_),
                  .string_value = {}};

    double value = 0.0;
    const char *first = num_str.data();
    const char *last = num_str.data() + num_str.size();
    auto [ptr, ec] = std::from_chars(first, last, value);

    if (ec != std::errc() || ptr != last) {
      diag_.report(tree::Severity::Error,
                   tree::Span(start_line, start_col, line_, col_),
                   std::string("invalid numeric literal '") +
                       std::string(num_str) + "'");
      value = 0.0;
    }

    t.float_value = value;
    return t;
  }

  tree::Token lex_identifier_or_keyword(int start_line, int start_col) {
    const size_t start_idx = i_;
    while (!at_end() &&
           ((std::isalnum(static_cast<unsigned char>(peek())) != 0) ||
            peek() == '_')) {
      advance_pos(1);
    }
    const std::string_view id = source_.substr(start_idx, i_ - start_idx);

    tree::Token t;
    t.span = tree::Span(start_line, start_col, line_, col_);
    auto it = keywords().find(id);
    if (it != keywords().end()) {
      t.type = it->second;
    } else {
      t.type = tree::TokenType::Identifier;
      t.string_value = std::string(id);
    }
    return t;
  }

  std::string decode_string_escapes(std::string_view raw, int start_line,
                                    int start_col) {
    std::string out_s;
    out_s.reserve(raw.size());

    for (size_t r = 0; r < raw.size(); ++r) {
      if (raw[r] != '\\') {
        out_s.push_back(raw[r]);
        continue;
      }

      if (++r >= raw.size()) {
        diag_.report(tree::Severity::Error,
                     tree::Span(start_line, start_col, line_, col_),
                     "unterminated escape sequence");
        break;
      }

      switch (raw[r]) {
      case '"':
        out_s.push_back('"');
        break;
      case '\\':
        out_s.push_back('\\');
        break;
      case 'n':
        out_s.push_back('\n');
        break;
      case 't':
        out_s.push_back('\t');
        break;
      case 'r':
        out_s.push_back('\r');
        break;
      default:
        diag_.report(tree::Severity::Error,
                     tree::Span(start_line, start_col, line_, col_),
                     std::string("unknown escape sequence '\\") + raw[r] + "'");
        break;
      }
    }

    return out_s;
  }

  tree::Token lex_string(int start_line, int start_col) {
    advance_pos(1);
    const size_t start_idx = i_;

    while (!at_end() && peek() != '"') {
      if (peek() == '\\' && i_ + 1 < source_.size()) {
        i_++;
        col_++;
        if (peek() == '\n') {
          line_++;
          col_ = 1;
        } else {
          col_++;
        }
        i_++;
        continue;
      }

      if (peek() == '\n') {
        line_++;
        col_ = 1;
      } else {
        col_++;
      }
      i_++;
    }

    const std::string_view raw = source_.substr(start_idx, i_ - start_idx);
    const bool unterminated = at_end();

    if (unterminated) {
      diag_.report(tree::Severity::Error,
                   tree::Span(start_line, start_col, line_, col_),
                   "unterminated string literal");
    } else {
      advance_pos(1);
    }

    std::string decoded = decode_string_escapes(raw, start_line, start_col);
    return tree::Token{.type = tree::TokenType::String,
                       .span = tree::Span(start_line, start_col, line_, col_),
                       .string_value = std::move(decoded)};
  }

  bool try_lex_operator(int start_line, int start_col, tree::Token &out) {
    tree::TokenType kind = tree::TokenType::Eof;
    size_t advance_len = 0;

    if (i_ + 1 < source_.size()) {
      if (auto it = double_ops().find(source_.substr(i_, 2));
          it != double_ops().end()) {
        kind = it->second;
        advance_len = 2;
      }
    }
    if (advance_len == 0) {
      if (auto op = lookup_single_op(peek())) {
        kind = *op;
        advance_len = 1;
      }
    }

    if (advance_len == 0) {
      return false;
    }

    advance_pos(advance_len);
    out = tree::Token{.type = kind,
                      .span = tree::Span(start_line, start_col, line_, col_),
                      .string_value = {}};
    return true;
  }
};

} // namespace

namespace tree {
std::vector<Token> lexer(std::string_view source, DiagnosticEngine &diag) {
  Lexer lexer(source, diag);
  return lexer.lexer();
}
} // namespace tree