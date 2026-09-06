#include "parser.hpp"
#include "diagnostic.hpp"
#include "span.hpp"
#include "token.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace {
constexpr std::array<std::pair<tree::TokenType, tree::BinaryOp>, 2>
    COMPARISON_OPS = {{
        {tree::TokenType::Equal, tree::BinaryOp::Equal},
        {tree::TokenType::LessThan, tree::BinaryOp::LessThan},
    }};

constexpr std::array<std::pair<tree::TokenType, tree::BinaryOp>, 2>
    ADDITIVE_OPS = {{
        {tree::TokenType::Plus, tree::BinaryOp::Add},
        {tree::TokenType::Minus, tree::BinaryOp::Sub},
    }};

constexpr std::array<std::pair<tree::TokenType, tree::BinaryOp>, 3>
    MULTIPLICATIVE_OPS = {{
        {tree::TokenType::Star, tree::BinaryOp::Mul},
        {tree::TokenType::Slash, tree::BinaryOp::Div},
        {tree::TokenType::Percent, tree::BinaryOp::Mod},
    }};

template <std::size_t N>
constexpr std::optional<tree::BinaryOp> lookup_op(
    const std::array<std::pair<tree::TokenType, tree::BinaryOp>, N> &table,
    tree::TokenType t) {
  for (auto &[k, v] : table) {
    if (k == t)
      return v;
  }
  return std::nullopt;
}

struct ParseAbort {};

class Parser {
public:
  Parser(std::vector<tree::Token> tokens, tree::DiagnosticEngine &diag)
      : tokens_(std::move(tokens)), diag_(diag) {}

  tree::Program parse_program() {
    tree::Program prog;
    while (!check(tree::TokenType::Eof)) {
      const size_t start_pos = pos_;
      try {
        prog.exprs.push_back(parse_top_level());
      } catch (const ParseAbort &) {
        synchronize(start_pos);
      }
    }
    return prog;
  }

private:
  std::vector<tree::Token> tokens_;
  tree::DiagnosticEngine &diag_;
  size_t pos_ = 0;

  [[nodiscard]] const tree::Token &peek(size_t offset = 0) const {
    const size_t idx = pos_ + offset;
    return idx < tokens_.size() ? tokens_[idx] : tokens_.back();
  }

  const tree::Token &advance() {
    const tree::Token &t = peek();
    if (pos_ + 1 < tokens_.size()) {
      pos_++;
    }
    return t;
  }

  [[nodiscard]] bool check(tree::TokenType type) const {
    return peek().type == type;
  }

  bool match(tree::TokenType type) {
    if (!check(type)) {
      return false;
    }
    advance();
    return true;
  }

  [[noreturn]] void error(tree::Span span, std::string message) {
    diag_.report(tree::Severity::Error, span, std::move(message));
    throw ParseAbort{};
  }

  const tree::Token &expect(tree::TokenType type, const char *what) {
    if (!check(type)) {
      error(peek().span, std::string("expected ") + what + " but found " +
                             std::string(tree::to_string(peek().type)));
    }
    return advance();
  }

  const tree::Token &expect_close(tree::TokenType type, const char *what,
                                  tree::Span open_span, const char *open_what) {
    if (!check(type)) {
      const tree::Span span{open_span.begin, peek().span.end};
      error(span, std::string("unclosed ") + open_what + ": expected " + what +
                      " but found " +
                      std::string(tree::to_string(peek().type)));
    }
    return advance();
  }

  void synchronize(size_t failed_at) {
    if (pos_ <= failed_at) {
      advance();
    }
    while (!check(tree::TokenType::Eof)) {
      if (check(tree::TokenType::Identifier)) {
        return;
      }
      advance();
    }
  }

  tree::PatternPtr expr_to_pattern(tree::ExprPtr expr) {
    const tree::Span span = expr->span;
    if (auto *lit = std::get_if<tree::FloatLiteral>(&expr->value)) {
      return make_pattern<tree::FloatPattern>(span, lit->value);
    }
    if (auto *lit = std::get_if<tree::StringLiteral>(&expr->value)) {
      return make_pattern<tree::StringPattern>(span, lit->value);
    }
    if (auto *id = std::get_if<tree::Identifier>(&expr->value)) {
      if (id->name == "_") {
        return make_pattern<tree::WildcardPattern>(span);
      }
      return make_pattern<tree::VarPattern>(span, id->name);
    }
    if (auto *tup = std::get_if<tree::TupleExpr>(&expr->value)) {
      std::vector<tree::TuplePatternField> fields;
      fields.reserve(tup->fields.size());
      std::ranges::transform(tup->fields, std::back_inserter(fields),
                             [this](auto &f) {
                               return tree::TuplePatternField{
                                   f.name, expr_to_pattern(std::move(f.value))};
                             });
      return make_pattern<tree::TuplePattern>(span, std::move(fields));
    }
    error(span, "invalid pattern");
  }

  tree::ExprPtr parse_top_level() {
    const tree::Span start = peek().span;
    tree::ExprPtr head = parse_postfix();

    if (!match(tree::TokenType::Assign)) {
      return head;
    }

    tree::ExprPtr body = parse_expr();
    const tree::Span span{start.begin, body->span.end};

    if (auto *call = std::get_if<tree::Call>(&head->value)) {
      if (auto *callee_id =
              std::get_if<tree::Identifier>(&call->callee->value)) {
        std::vector<tree::PatternPtr> params;
        params.reserve(call->args.size());
        std::ranges::transform(
            call->args, std::back_inserter(params),
            [this](auto &arg) { return expr_to_pattern(std::move(arg)); });
        return make_expr<tree::FunctionClause>(
            span, callee_id->name, std::move(params), std::move(body));
      }
    }

    return make_expr<tree::Assignment>(span, expr_to_pattern(std::move(head)),
                                       std::move(body));
  }

  tree::ExprPtr parse_expr() { return parse_comparison(); }

  tree::ExprPtr parse_comparison() {
    return parse_binary_level(COMPARISON_OPS, &Parser::parse_additive);
  }

  tree::ExprPtr parse_additive() {
    return parse_binary_level(ADDITIVE_OPS, &Parser::parse_multiplicative);
  }

  tree::ExprPtr parse_multiplicative() {
    return parse_binary_level(MULTIPLICATIVE_OPS, &Parser::parse_unary);
  }

  template <std::size_t N>
  tree::ExprPtr parse_binary_level(
      const std::array<std::pair<tree::TokenType, tree::BinaryOp>, N> &ops,
      tree::ExprPtr (Parser::*next)()) {
    tree::ExprPtr lhs = (this->*next)();
    for (;;) {
      auto op = lookup_op(ops, peek().type);
      if (!op) {
        break;
      }
      advance();
      tree::ExprPtr rhs = (this->*next)();
      const tree::Span span{lhs->span.begin, rhs->span.end};
      lhs = make_expr<tree::BinaryExpr>(span, *op, std::move(lhs),
                                        std::move(rhs));
    }
    return lhs;
  }

  tree::ExprPtr parse_unary() {
    if (check(tree::TokenType::Minus)) {
      const tree::Span start = peek().span;
      advance();
      tree::ExprPtr operand = parse_unary();
      const tree::Span span{start.begin, operand->span.end};
      tree::ExprPtr zero = make_expr<tree::FloatLiteral>(start, 0.0);
      return make_expr<tree::BinaryExpr>(span, tree::BinaryOp::Sub,
                                         std::move(zero), std::move(operand));
    }
    return parse_postfix();
  }

  tree::ExprPtr parse_postfix() {
    tree::ExprPtr expr = parse_primary();
    for (;;) {
      if (match(tree::TokenType::Dot)) {
        if (check(tree::TokenType::Float)) {
          const tree::Token idx_tok = advance();
          const tree::Span span{expr->span.begin, idx_tok.span.end};
          const double f = idx_tok.float_value;
          if (f != std::floor(f) || f < 0) {
            error(idx_tok.span,
                  "tuple field index must be a non-negative integer");
          }
          auto idx = static_cast<int64_t>(f);
          expr = make_expr<tree::FieldAccess>(span, std::move(expr),
                                              tree::FieldKey{idx});
        } else if (check(tree::TokenType::Identifier)) {
          tree::Token name_tok = advance();
          const tree::Span span{expr->span.begin, name_tok.span.end};
          expr = make_expr<tree::FieldAccess>(
              span, std::move(expr), tree::FieldKey{name_tok.string_value});
        } else {
          error(peek().span, "expected field index or name after '.'");
        }
      } else if (check(tree::TokenType::LeftParen) &&
                 peek().span.begin.line == expr->span.end.line) {
        expr = parse_call(std::move(expr));
      } else {
        break;
      }
    }
    return expr;
  }

  tree::ExprPtr parse_call(tree::ExprPtr callee) {
    const tree::Span start = callee->span;
    const tree::Token open = expect(tree::TokenType::LeftParen, "'('");
    std::vector<tree::ExprPtr> args;
    if (!check(tree::TokenType::RightParen)) {
      args.push_back(parse_expr());
      while (match(tree::TokenType::Comma)) {
        args.push_back(parse_expr());
      }
    }
    const tree::Token close =
        expect_close(tree::TokenType::RightParen, "')'", open.span, "'('");
    const tree::Span span{start.begin, close.span.end};
    return make_expr<tree::Call>(span, std::move(callee), std::move(args));
  }

  tree::ExprPtr parse_primary() {
    const tree::Token &t = peek();
    switch (t.type) {
    case tree::TokenType::Float:
      advance();
      return make_expr<tree::FloatLiteral>(t.span, t.float_value);
    case tree::TokenType::String:
      advance();
      return make_expr<tree::StringLiteral>(t.span, t.string_value);
    case tree::TokenType::Identifier:
      advance();
      return make_expr<tree::Identifier>(t.span, t.string_value);
    case tree::TokenType::LeftParen:
      return parse_tuple_or_paren();
    case tree::TokenType::Backslash:
      return parse_lambda();
    case tree::TokenType::If:
      return parse_if();
    default:
      error(t.span, "unexpected token " + std::string(tree::to_string(t.type)));
    }
  }

  tree::ExprPtr parse_tuple_or_paren() {
    const tree::Span start = peek().span;
    const tree::Token open = expect(tree::TokenType::LeftParen, "'('");

    if (check(tree::TokenType::RightParen)) {
      const tree::Token close = advance();
      const tree::Span span{start.begin, close.span.end};
      return make_expr<tree::TupleExpr>(span, tree::TupleExpr{});
    }

    std::vector<tree::TupleExprField> fields;
    fields.push_back(parse_tuple_field());
    bool saw_comma = false;
    while (match(tree::TokenType::Comma)) {
      saw_comma = true;
      fields.push_back(parse_tuple_field());
    }
    const tree::Token close =
        expect_close(tree::TokenType::RightParen, "')'", open.span, "'('");
    const tree::Span span{start.begin, close.span.end};

    if (!saw_comma && fields.size() == 1 && !fields[0].name) {
      return std::move(fields[0].value);
    }

    return make_expr<tree::TupleExpr>(span, std::move(fields));
  }

  tree::TupleExprField parse_tuple_field() {
    if (check(tree::TokenType::Identifier) &&
        peek(1).type == tree::TokenType::Colon) {
      std::string name = advance().string_value;
      advance();
      tree::ExprPtr value = parse_expr();
      return tree::TupleExprField{
          .name = std::optional<std::string>(std::move(name)),
          .value = std::move(value)};
    }
    return tree::TupleExprField{.name = std::nullopt, .value = parse_expr()};
  }

  tree::ExprPtr parse_lambda() {
    const tree::Span start = peek().span;
    expect(tree::TokenType::Backslash, "'\\'");

    std::vector<tree::PatternPtr> params;

    if (check(tree::TokenType::LeftParen)) {
      const tree::Token open = advance();
      if (!check(tree::TokenType::RightParen)) {
        params.push_back(expr_to_pattern(parse_postfix()));
        while (match(tree::TokenType::Comma)) {
          params.push_back(expr_to_pattern(parse_postfix()));
        }
      }
      expect_close(tree::TokenType::RightParen, "')'", open.span, "'('");
    } else {
      params.push_back(expr_to_pattern(parse_postfix()));
    }

    expect(tree::TokenType::Arrow, "'->'");
    tree::ExprPtr body = parse_expr();
    const tree::Span span{start.begin, body->span.end};
    return make_expr<tree::Lambda>(span, std::move(params), std::move(body));
  }

  tree::ExprPtr parse_if() {
    const tree::Span start = peek().span;
    expect(tree::TokenType::If, "'if'");
    tree::ExprPtr cond = parse_expr();
    expect(tree::TokenType::Then, "'then'");
    tree::ExprPtr then_branch = parse_expr();
    expect(tree::TokenType::Else, "'else'");
    tree::ExprPtr else_branch = parse_expr();
    const tree::Span span{start.begin, else_branch->span.end};
    return make_expr<tree::IfExpr>(
        span, std::move(cond), std::move(then_branch), std::move(else_branch));
  }
};
} // namespace

namespace tree {
Program parse(std::vector<Token> tokens, DiagnosticEngine &diag) {
  Parser parser(std::move(tokens), diag);
  return parser.parse_program();
}
} // namespace tree