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
  auto r = lex("   \n\t @[ this is a comment ]@\n   ");
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
  auto r = lex("bar\nfoo");
  CHECK(r.tokens[0].type == TokenType::Identifier);
  CHECK_EQ(r.tokens[0].span.begin.line, 1);
  CHECK_EQ(r.tokens[0].span.begin.column, 1);

  CHECK(r.tokens[1].type == TokenType::Identifier);
  CHECK_EQ(r.tokens[1].span.begin.line, 2);
  CHECK_EQ(r.tokens[1].span.begin.column, 1);
}

TEST(small_program) {
  auto r = lex(R"(select(x == 1, "yes", "no"))");
  std::vector<TokenType> expected = {
      TokenType::Identifier, TokenType::LeftParen, TokenType::Identifier,
      TokenType::Equal,      TokenType::Float,     TokenType::Comma,
      TokenType::String,     TokenType::Comma,     TokenType::String,
      TokenType::RightParen, TokenType::Eof};
  CHECK_EQ(r.tokens.size(), expected.size());
  for (size_t idx = 0; idx < expected.size() && idx < r.tokens.size(); ++idx) {
    CHECK(r.tokens[idx].type == expected[idx]);
  }
  CHECK(!r.diag.has_errors());
}

TEST(token_spans_are_half_open) {
  auto r = lex("42 foo + == \"hi\"");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(6));
  CHECK_EQ(r.tokens[0].span.begin.line, 1);
  CHECK_EQ(r.tokens[0].span.begin.column, 1);
  CHECK_EQ(r.tokens[0].span.end.line, 1);
  CHECK_EQ(r.tokens[0].span.end.column, 3);
  CHECK_EQ(r.tokens[1].span.begin.column, 4);
  CHECK_EQ(r.tokens[1].span.end.column, 7);
  CHECK_EQ(r.tokens[2].span.begin.column, 8);
  CHECK_EQ(r.tokens[2].span.end.column, 9);
  CHECK_EQ(r.tokens[3].span.begin.column, 10);
  CHECK_EQ(r.tokens[3].span.end.column, 12);
  CHECK_EQ(r.tokens[4].span.begin.column, 13);
  CHECK_EQ(r.tokens[4].span.end.column, 17);
}

TEST(operator_spans) {
  auto r = lex("+ - * / % = < == \\");
  CHECK_EQ(r.tokens[0].span.begin.column, 1);
  CHECK_EQ(r.tokens[0].span.end.column, 2);
  CHECK_EQ(r.tokens[1].span.begin.column, 3);
  CHECK_EQ(r.tokens[1].span.end.column, 4);
  CHECK_EQ(r.tokens[2].span.begin.column, 5);
  CHECK_EQ(r.tokens[2].span.end.column, 6);
  CHECK_EQ(r.tokens[3].span.begin.column, 7);
  CHECK_EQ(r.tokens[3].span.end.column, 8);
  CHECK_EQ(r.tokens[4].span.begin.column, 9);
  CHECK_EQ(r.tokens[4].span.end.column, 10);
  CHECK_EQ(r.tokens[5].span.begin.column, 11);
  CHECK_EQ(r.tokens[5].span.end.column, 12);
  CHECK_EQ(r.tokens[6].span.begin.column, 13);
  CHECK_EQ(r.tokens[6].span.end.column, 14);
  CHECK_EQ(r.tokens[7].span.begin.column, 15);
  CHECK_EQ(r.tokens[7].span.end.column, 17);
  CHECK_EQ(r.tokens[8].span.begin.column, 18);
  CHECK_EQ(r.tokens[8].span.end.column, 19);
}

TEST(escaped_newline_is_line_continuation) {
  auto r = lex("\"hello\\\nworld\"");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::String);
  CHECK_EQ(r.tokens[0].string_value, std::string("helloworld"));
  CHECK_EQ(r.tokens[0].span.begin.line, 1);
  CHECK_EQ(r.tokens[0].span.begin.column, 1);
  CHECK_EQ(r.tokens[0].span.end.line, 2);
  CHECK_EQ(r.tokens[0].span.end.column, 7);
  CHECK(!r.diag.has_errors());
}

TEST(escaped_newline_can_be_used_multiple_times) {
  auto r = lex("\"a\\\nb\\\nc\"");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::String);
  CHECK_EQ(r.tokens[0].string_value, std::string("abc"));
  CHECK_EQ(r.tokens[0].span.begin.line, 1);
  CHECK_EQ(r.tokens[0].span.begin.column, 1);
  CHECK_EQ(r.tokens[0].span.end.line, 3);
  CHECK_EQ(r.tokens[0].span.end.column, 3);
  CHECK(!r.diag.has_errors());
}

TEST(string_with_raw_newline) {
  auto r = lex("\"hello\nworld\"");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::String);
  CHECK(!r.diag.has_errors());
  CHECK_EQ(r.tokens[0].span.begin.line, 1);
  CHECK_EQ(r.tokens[0].span.begin.column, 1);
  CHECK_EQ(r.tokens[0].span.end.line, 2);
  CHECK_EQ(r.tokens[0].span.end.column, 7);
}

TEST(string_span_includes_quotes) {
  auto r = lex("\"hello\"");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(2));
  CHECK(r.tokens[0].type == TokenType::String);
  CHECK_EQ(r.tokens[0].span.begin.line, 1);
  CHECK_EQ(r.tokens[0].span.begin.column, 1);
  CHECK_EQ(r.tokens[0].span.end.line, 1);
  CHECK_EQ(r.tokens[0].span.end.column, 8);
}

TEST(number_literal_edge_cases) {
  auto r = lex("0 0.0 1.0 1000000.25");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(5));
  CHECK(r.tokens[0].type == TokenType::Float);
  CHECK_EQ(r.tokens[0].float_value, 0.0);
  CHECK(r.tokens[1].type == TokenType::Float);
  CHECK_EQ(r.tokens[1].float_value, 0.0);
  CHECK_EQ(r.tokens[2].float_value, 1.0);
  CHECK_EQ(r.tokens[3].float_value, 1000000.25);
}

TEST(comment_ends_at_newline) {
  auto r = lex("42 @[ comment ]@\n 43");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(3));
  CHECK(r.tokens[0].type == TokenType::Float);
  CHECK_EQ(r.tokens[0].float_value, 42.0);
  CHECK(r.tokens[1].type == TokenType::Float);
  CHECK_EQ(r.tokens[1].float_value, 43.0);
  CHECK_EQ(r.tokens[1].span.begin.line, 2);
  CHECK_EQ(r.tokens[1].span.begin.column, 2);
  CHECK(!r.diag.has_errors());
}

TEST(keyword_prefixes_remain_identifiers) {
  auto r = lex("iff iffy thenx else_");
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(5));
  CHECK(r.tokens[0].type == TokenType::Identifier);
  CHECK_EQ(r.tokens[0].string_value, std::string("iff"));
  CHECK(r.tokens[1].type == TokenType::Identifier);
  CHECK_EQ(r.tokens[1].string_value, std::string("iffy"));
  CHECK(r.tokens[2].type == TokenType::Identifier);
  CHECK_EQ(r.tokens[2].string_value, std::string("thenx"));
  CHECK(r.tokens[3].type == TokenType::Identifier);
  CHECK_EQ(r.tokens[3].string_value, std::string("else_"));
}

TEST(unexpected_characters_recover_repeatedly) {
  auto r = lex("@ 1 $ 2 @ 3");
  CHECK(r.diag.has_errors());
  CHECK_EQ(r.diag.count(Severity::Error), static_cast<size_t>(3));
  CHECK_EQ(r.tokens.size(), static_cast<size_t>(4));
  CHECK_EQ(r.tokens[0].float_value, 1.0);
  CHECK_EQ(r.tokens[1].float_value, 2.0);
  CHECK_EQ(r.tokens[2].float_value, 3.0);
  CHECK(r.tokens[3].type == TokenType::Eof);
}

TEST(unknown_escape_reports_at_escape_location) {
  auto r = lex("\"hello\nworld\\qtest\"");
  CHECK(r.diag.has_errors());
  CHECK_EQ(r.diag.count(Severity::Error), static_cast<size_t>(1));
  std::ostringstream output;
  r.diag.print_all(output);
  const std::string text = output.str();
  CHECK(text.find("<test>:2:6") != std::string::npos);
  CHECK(text.find("hello") == std::string::npos);
  CHECK(text.find("world\\qtest") != std::string::npos);
  CHECK(text.find("     ^^") != std::string::npos);
}

TEST(unknown_escape_reports_at_escape_location_single_line) {
  auto r = lex(R"("abc\qdef")");
  CHECK(r.diag.has_errors());
  std::ostringstream output;
  r.diag.print_all(output);
  const std::string text = output.str();
  CHECK(text.find("<test>:1:5") != std::string::npos);
  CHECK(text.find("    ^^") != std::string::npos);
}
