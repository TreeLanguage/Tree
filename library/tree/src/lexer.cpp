#include "lexer.hpp"
#include "diagnostic.hpp"
#include "span.hpp"
#include "token.hpp"
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
const std::unordered_map<std::string_view, tree::TokenType> keywords = {
    {"if", tree::TokenType::If},
    {"then", tree::TokenType::Then},
    {"else", tree::TokenType::Else}};

const std::unordered_map<std::string_view, tree::TokenType> double_ops = {
    {"==", tree::TokenType::Equal}, {"->", tree::TokenType::Arrow}};

const std::unordered_map<char, tree::TokenType> single_ops = {
    {'(', tree::TokenType::LeftParen}, {')', tree::TokenType::RightParen},
    {',', tree::TokenType::Comma},     {'.', tree::TokenType::Dot},
    {':', tree::TokenType::Colon},     {'+', tree::TokenType::Plus},
    {'*', tree::TokenType::Star},      {'/', tree::TokenType::Slash},
    {'%', tree::TokenType::Percent},   {'=', tree::TokenType::Assign},
    {'<', tree::TokenType::LessThan},  {'\\', tree::TokenType::Backslash},
    {'-', tree::TokenType::Minus}};

struct LexerState {
  std::string_view source;
  tree::DiagnosticEngine &diag;
  size_t i = 0;
  int line = 1;
  int col = 1;

  bool at_end() const { return i >= source.size(); }
  char peek() const { return source[i]; }
  char peek(size_t offset) const { return source[i + offset]; }

  void advance_pos(size_t len, char ch = '\0') {
    if (ch == '\n') {
      line++;
      col = 1;
    } else {
      col += static_cast<int>(len);
    }
    i += len;
  }
};

inline static void skip_whitespace_and_comments(LexerState &st) {
  while (!st.at_end()) {
    char c = st.peek();
    if (c == '\n') {
      st.advance_pos(1, '\n');
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      st.advance_pos(1);
    } else if (c == '#') {
      while (!st.at_end() && st.peek() != '\n') {
        st.advance_pos(1);
      }
    } else {
      break;
    }
  }
}

inline static tree::Token lex_number(LexerState &st, int start_line,
                                     int start_col) {
  size_t start_idx = st.i;

  while (!st.at_end() && std::isdigit(static_cast<unsigned char>(st.peek()))) {
    st.advance_pos(1);
  }
  if (!st.at_end() && st.peek() == '.' && st.i + 1 < st.source.size() &&
      std::isdigit(static_cast<unsigned char>(st.peek(1)))) {
    st.advance_pos(1);
    while (!st.at_end() &&
           std::isdigit(static_cast<unsigned char>(st.peek()))) {
      st.advance_pos(1);
    }
  }

  std::string_view num_str = st.source.substr(start_idx, st.i - start_idx);
  tree::Token t{tree::TokenType::Float,
                tree::Span(start_line, start_col, st.line, st.col)};
  t.float_value = std::stod(std::string(num_str));
  return t;
}

inline static tree::Token
lex_identifier_or_keyword(LexerState &st, int start_line, int start_col) {
  size_t start_idx = st.i;
  while (!st.at_end() && (std::isalnum(static_cast<unsigned char>(st.peek())) ||
                          st.peek() == '_')) {
    st.advance_pos(1);
  }
  std::string_view id = st.source.substr(start_idx, st.i - start_idx);

  tree::Token t;
  t.span = tree::Span(start_line, start_col, st.line, st.col);
  auto it = keywords.find(id);
  if (it != keywords.end()) {
    t.type = it->second;
  } else {
    t.type = tree::TokenType::Identifier;
    t.string_value = std::string(id);
  }
  return t;
}

inline static std::string decode_string_escapes(LexerState &st,
                                                std::string_view raw,
                                                int start_line, int start_col) {
  std::string out_s;
  out_s.reserve(raw.size());

  for (size_t r = 0; r < raw.size(); ++r) {
    if (raw[r] != '\\') {
      out_s.push_back(raw[r]);
      continue;
    }

    if (++r >= raw.size()) {
      st.diag.report(tree::Severity::Error,
                     tree::Span(start_line, start_col, st.line, st.col),
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
      st.diag.report(tree::Severity::Error,
                     tree::Span(start_line, start_col, st.line, st.col),
                     std::string("unknown escape sequence '\\") + raw[r] + "'");
      break;
    }
  }

  return out_s;
}

inline static tree::Token lex_string(LexerState &st, int start_line,
                                     int start_col) {
  st.advance_pos(1);
  size_t start_idx = st.i;

  while (!st.at_end() && st.peek() != '"') {
    if (st.peek() == '\n') {
      st.line++;
      st.col = 1;
    } else {
      st.col++;
    }
    if (st.peek() == '\\' && st.i + 1 < st.source.size()) {
      st.i++;
      st.col++;
    }
    st.i++;
  }

  std::string_view raw = st.source.substr(start_idx, st.i - start_idx);
  if (st.at_end()) {
    st.diag.report(tree::Severity::Error,
                   tree::Span(start_line, start_col, st.line, st.col),
                   "unterminated string literal");
  }

  std::string decoded = decode_string_escapes(st, raw, start_line, start_col);

  tree::Token t{tree::TokenType::String,
                tree::Span(start_line, start_col, st.line, st.col)};
  t.string_value = std::move(decoded);
  if (!st.at_end()) {
    st.advance_pos(1);
  }
  return t;
}

inline static bool try_lex_operator(LexerState &st, int start_line,
                                    int start_col, tree::Token &out) {
  tree::TokenType kind = tree::TokenType::Eof;
  size_t advance = 0;

  if (st.i + 1 < st.source.size()) {
    if (auto it = double_ops.find(st.source.substr(st.i, 2));
        it != double_ops.end()) {
      kind = it->second;
      advance = 2;
    }
  }
  if (advance == 0) {
    if (auto it = single_ops.find(st.peek()); it != single_ops.end()) {
      kind = it->second;
      advance = 1;
    }
  }

  if (advance == 0) {
    return false;
  }

  out = tree::Token{kind, tree::Span(start_line, start_col, st.line,
                                     st.col + static_cast<int>(advance) - 1)};
  st.advance_pos(advance);
  return true;
}

} // namespace

namespace tree {

std::vector<Token> lexer(std::string_view source, DiagnosticEngine &diag) {
  std::vector<Token> out;
  LexerState st{source, diag};

  while (!st.at_end()) {
    skip_whitespace_and_comments(st);
    if (st.at_end()) {
      break;
    }

    char c = st.peek();
    int start_line = st.line, start_col = st.col;

    if (std::isdigit(static_cast<unsigned char>(c))) {
      out.push_back(lex_number(st, start_line, start_col));
      continue;
    }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      out.push_back(lex_identifier_or_keyword(st, start_line, start_col));
      continue;
    }

    if (c == '"') {
      out.push_back(lex_string(st, start_line, start_col));
      continue;
    }

    Token op_token;
    if (try_lex_operator(st, start_line, start_col, op_token)) {
      out.push_back(std::move(op_token));
      continue;
    }

    diag.report(Severity::Error,
                Span(start_line, start_col, start_line, start_col + 1),
                std::string("unexpected character '") + c + "'");
    st.advance_pos(1);
  }

  out.push_back({TokenType::Eof, Span(st.line, st.col, st.line, st.col)});
  return out;
}

} // namespace tree