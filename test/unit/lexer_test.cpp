#include "diagnostic.hpp"
#include "framework.hpp"
#include "lexer.hpp"
#include "span.hpp"
#include "token.hpp"
#include <cstddef>
#include <string>
#include <vector>

using tree::DiagnosticEngine;
using tree::lexer;
using tree::Severity;
using tree::Token;
using tree::TokenType;

namespace {

struct LexResult {
  std::vector<Token> tokens;
  DiagnosticEngine diag;
};

LexResult lex(const std::string &src) {
  LexResult result{.tokens = {}, .diag = DiagnosticEngine("<test>", src)};
  result.tokens = lexer(src, result.diag);
  return result;
}

void expect_trailing_eof(const std::vector<Token> &tokens) {
  CHECK(!tokens.empty());
  if (!tokens.empty()) {
    CHECK(tokens.back().type == TokenType::Eof);
  }
}

} // namespace

TEST(empty_source_yields_only_eof) {
  auto r = lex("");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(1));
  expect_trailing_eof(r.tokens);
  CHECK(!r.diag.has_errors());
}

TEST(whitespace_and_comments_are_skipped) {
  auto r = lex("   \n\t # this is a comment\n   ");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(1));
  expect_trailing_eof(r.tokens);
  CHECK(!r.diag.has_errors());
}

TEST(integer_literal) {
  auto r = lex("42");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::Float);
  CHECK_EQ(r.tokens[0].float_value, 42.0);
  CHECK(!r.diag.has_errors());
}

TEST(float_literal) {
  auto r = lex("3.14");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::Float);
  CHECK_EQ(r.tokens[0].float_value, 3.14);
}

TEST(identifier) {
  auto r = lex("foo_bar1");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::Identifier);
  CHECK_EQ(r.tokens[0].string_value, std::string("foo_bar1"));
}

TEST(keywords_if_then_else) {
  auto r = lex("if then else");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(4));
  CHECK(r.tokens[0].type == TokenType::If);
  CHECK(r.tokens[1].type == TokenType::Then);
  CHECK(r.tokens[2].type == TokenType::Else);
}

TEST(keyword_prefix_is_still_identifier) {
  auto r = lex("iffy");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::Identifier);
  CHECK_EQ(r.tokens[0].string_value, std::string("iffy"));
}

TEST(simple_string) {
  auto r = lex("\"hello\"");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::String);
  CHECK_EQ(r.tokens[0].string_value, std::string("hello"));
  CHECK(!r.diag.has_errors());
}

TEST(string_escape_sequences) {
  auto r = lex(R"("a\n\t\r\"\\b")");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::String);
  CHECK_EQ(r.tokens[0].string_value, std::string("a\n\t\r\"\\b"));
  CHECK(!r.diag.has_errors());
}

TEST(unknown_escape_reports_error_but_recovers) {
  auto r = lex(R"("a\qb")");
  CHECK(r.diag.has_errors());
  CHECK_EQ(r.diag.count(Severity::Error), static_cast<size_t>(1));
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::String);
}

TEST(unterminated_string_reports_error_but_recovers) {
  auto r = lex("\"abc");
  CHECK(r.diag.has_errors());
  expect_trailing_eof(r.tokens);
}

TEST(trailing_backslash_at_eof_reports_error) {
  auto r = lex("\"abc\\");
  CHECK(r.diag.has_errors());
  expect_trailing_eof(r.tokens);
}

TEST(single_char_operators) {
  auto r = lex("(),:+-*/%=<\\");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(13));
  CHECK(r.tokens[0].type == TokenType::LeftParen);
  CHECK(r.tokens[1].type == TokenType::RightParen);
  CHECK(r.tokens[2].type == TokenType::Comma);
  CHECK(r.tokens[3].type == TokenType::Colon);
  CHECK(r.tokens[4].type == TokenType::Plus);
  CHECK(r.tokens[5].type == TokenType::Minus);
  CHECK(r.tokens[6].type == TokenType::Star);
  CHECK(r.tokens[7].type == TokenType::Slash);
  CHECK(r.tokens[8].type == TokenType::Percent);
  CHECK(r.tokens[9].type == TokenType::Assign);
  CHECK(r.tokens[10].type == TokenType::LessThan);
  CHECK(r.tokens[11].type == TokenType::Backslash);
}

TEST(double_char_operator_equal) {
  auto r = lex("==");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::Equal);
}

TEST(single_equal_is_assign_not_equal) {
  auto r = lex("=");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::Assign);
}

TEST(unexpected_character_reports_error_and_skips) {
  auto r = lex("@");
  CHECK(r.diag.has_errors());
  CHECK_EQ(r.diag.count(Severity::Error), static_cast<size_t>(1));
  expect_trailing_eof(r.tokens);
}

TEST(unexpected_character_does_not_stop_lexing) {
  auto r = lex("@ 42");
  CHECK(r.diag.has_errors());
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::Float);
  CHECK_EQ(r.tokens[0].float_value, 42.0);
}

TEST(span_tracks_line_and_column) {
  auto r = lex("if\nfoo");
  CHECK(r.tokens[0].type == TokenType::If);
  CHECK_EQ(r.tokens[0].span.begin.line, 1);
  CHECK_EQ(r.tokens[0].span.begin.column, 1);

  CHECK(r.tokens[1].type == TokenType::Identifier);
  CHECK_EQ(r.tokens[1].span.begin.line, 2);
  CHECK_EQ(r.tokens[1].span.begin.column, 1);
}

TEST(small_program) {
  auto r = lex(R"(if x == 1 then "yes" else "no")");
  std::vector<TokenType> expected = {
      TokenType::If,    TokenType::Identifier, TokenType::Equal,
      TokenType::Float, TokenType::Then,       TokenType::String,
      TokenType::Else,  TokenType::String,     TokenType::Eof};

  CHECK_EQ(r.tokens.size(), expected.size());
  for (size_t idx = 0; idx < expected.size() && idx < r.tokens.size(); ++idx) {
    CHECK(r.tokens[idx].type == expected[idx]);
  }
  CHECK(!r.diag.has_errors());
}