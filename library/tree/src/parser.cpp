#include "parser.hpp"
#include "ast.hpp"
#include "diagnostic.hpp"
#include "span.hpp"
#include "token.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
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
      : tokens_(std::move(tokens)), diag_(diag) {
    prog_.arena.reserve(tokens_.size(), tokens_.size() / 4);
  }

  tree::Program parse_program() {
    while (!check(tree::TokenType::Eof)) {
      const size_t start_pos = pos_;
      try {
        prog_.exprs.push_back(parse_top_level());
      } catch (const ParseAbort &) {
        synchronize(start_pos);
      }
    }
    return std::move(prog_);
  }

private:
  tree::Program prog_;
  std::vector<tree::Token> tokens_;
  tree::DiagnosticEngine &diag_;
  size_t pos_ = 0;

  template <typename T, typename... Args>
  tree::ExprId make_expr(tree::Span span, Args &&...args) {
    return prog_.arena.make_expr<T>(span, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  tree::PatternId make_pattern(tree::Span span, Args &&...args) {
    return prog_.arena.make_pattern<T>(span, std::forward<Args>(args)...);
  }

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

  tree::PatternId expr_to_pattern(tree::ExprId expr_id) {
    const tree::Expr &expr = prog_.arena.get(expr_id);
    const tree::Span span = expr.span;
    if (const auto *lit = std::get_if<tree::FloatLiteral>(&expr.value)) {
      return make_pattern<tree::FloatPattern>(span, lit->value);
    }
    if (const auto *lit = std::get_if<tree::StringLiteral>(&expr.value)) {
      return make_pattern<tree::StringPattern>(span, lit->value);
    }
    if (const auto *id = std::get_if<tree::Identifier>(&expr.value)) {
      if (id->name == "_") {
        return make_pattern<tree::WildcardPattern>(span);
      }
      return make_pattern<tree::VarPattern>(span, id->name);
    }
    if (const auto *tup = std::get_if<tree::TupleExpr>(&expr.value)) {
      std::vector<tree::TuplePatternField> fields;
      fields.reserve(tup->fields.size());
      std::ranges::transform(
          tup->fields, std::back_inserter(fields), [this](auto &f) {
            return tree::TuplePatternField{f.name, expr_to_pattern(f.value)};
          });
      return make_pattern<tree::TuplePattern>(span, std::move(fields));
    }
    error(span, "invalid pattern");
  }

  tree::ExprId parse_top_level() {
    const tree::Span start = peek().span;
    const tree::ExprId head = parse_unary();

    if (!match(tree::TokenType::Assign)) {
      tree::ExprId expr =
          continue_binary_level(MULTIPLICATIVE_OPS, &Parser::parse_unary, head);
      expr = continue_binary_level(ADDITIVE_OPS, &Parser::parse_multiplicative,
                                   expr);
      expr =
          continue_binary_level(COMPARISON_OPS, &Parser::parse_additive, expr);
      return expr;
    }

    const tree::ExprId body = parse_expr();
    const tree::Span span{start.begin, prog_.arena.get(body).span.end};

    const tree::Expr &head_expr = prog_.arena.get(head);
    if (const auto *call = std::get_if<tree::Call>(&head_expr.value)) {
      const tree::Expr &callee_expr = prog_.arena.get(call->callee);
      if (const auto *callee_id =
              std::get_if<tree::Identifier>(&callee_expr.value)) {
        std::string name = callee_id->name;
        std::vector<tree::ExprId> args = call->args;
        std::vector<tree::PatternId> params;
        params.reserve(args.size());
        std::ranges::transform(
            args, std::back_inserter(params),
            [this](const auto &arg) { return expr_to_pattern(arg); });
        return make_expr<tree::FunctionClause>(span, std::move(name),
                                               std::move(params), body);
      }
    }

    return make_expr<tree::Assignment>(span, expr_to_pattern(head), body);
  }

  tree::ExprId parse_expr() { return parse_comparison(); }

  tree::ExprId parse_comparison() {
    return parse_binary_level(COMPARISON_OPS, &Parser::parse_additive);
  }

  tree::ExprId parse_additive() {
    return parse_binary_level(ADDITIVE_OPS, &Parser::parse_multiplicative);
  }

  tree::ExprId parse_multiplicative() {
    return parse_binary_level(MULTIPLICATIVE_OPS, &Parser::parse_unary);
  }

  template <std::size_t N>
  tree::ExprId continue_binary_level(
      const std::array<std::pair<tree::TokenType, tree::BinaryOp>, N> &ops,
      tree::ExprId (Parser::*next)(), tree::ExprId lhs) {
    for (;;) {
      auto op = lookup_op(ops, peek().type);
      if (!op) {
        break;
      }
      advance();
      const tree::ExprId rhs = (this->*next)();
      const tree::Span span{prog_.arena.get(lhs).span.begin,
                            prog_.arena.get(rhs).span.end};
      lhs = make_expr<tree::BinaryExpr>(span, *op, lhs, rhs);
    }
    return lhs;
  }

  template <std::size_t N>
  tree::ExprId parse_binary_level(
      const std::array<std::pair<tree::TokenType, tree::BinaryOp>, N> &ops,
      tree::ExprId (Parser::*next)()) {
    const tree::ExprId lhs = (this->*next)();
    return continue_binary_level(ops, next, lhs);
  }

  tree::ExprId parse_unary() {
    if (check(tree::TokenType::Minus)) {
      const tree::Span start = peek().span;
      advance();
      const tree::ExprId operand = parse_unary();
      const tree::Span span{start.begin, prog_.arena.get(operand).span.end};
      const tree::ExprId zero = make_expr<tree::FloatLiteral>(start, 0.0);
      return make_expr<tree::BinaryExpr>(span, tree::BinaryOp::Sub, zero,
                                         operand);
    }
    return parse_postfix();
  }

  tree::ExprId parse_postfix() {
    tree::ExprId expr = parse_primary();
    for (;;) {
      if (check(tree::TokenType::LeftParen) &&
          peek().span.begin.line == prog_.arena.get(expr).span.end.line) {
        expr = parse_call(expr);
      } else {
        break;
      }
    }
    return expr;
  }

  tree::ExprId parse_call(tree::ExprId callee) {
    const tree::Span start = prog_.arena.get(callee).span;
    const tree::Token open = expect(tree::TokenType::LeftParen, "'('");
    std::vector<tree::ExprId> args;
    if (!check(tree::TokenType::RightParen)) {
      args.push_back(parse_expr());
      while (match(tree::TokenType::Comma)) {
        args.push_back(parse_expr());
      }
    }
    const tree::Token close =
        expect_close(tree::TokenType::RightParen, "')'", open.span, "'('");
    const tree::Span span{start.begin, close.span.end};
    return make_expr<tree::Call>(span, callee, std::move(args));
  }

  tree::ExprId parse_primary() {
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

  tree::ExprId parse_tuple_or_paren() {
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
      tree::ExprId value = parse_expr();
      return tree::TupleExprField{
          .name = std::optional<std::string>(std::move(name)),
          .value = std::move(value)};
    }
    return tree::TupleExprField{.name = std::nullopt, .value = parse_expr()};
  }

  tree::ExprId parse_lambda() {
    const tree::Span start = peek().span;
    expect(tree::TokenType::Backslash, "'\\'");

    std::vector<tree::PatternId> params;

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
    const tree::ExprId body = parse_expr();
    const tree::Span span{start.begin, prog_.arena.get(body).span.end};
    return make_expr<tree::Lambda>(span, std::move(params), body);
  }

  tree::ExprId parse_if() {
    const tree::Span start = peek().span;
    expect(tree::TokenType::If, "'if'");
    const tree::ExprId cond = parse_expr();
    expect(tree::TokenType::Then, "'then'");
    const tree::ExprId then_branch = parse_expr();
    expect(tree::TokenType::Else, "'else'");
    const tree::ExprId else_branch = parse_expr();
    const tree::Span span{start.begin, prog_.arena.get(else_branch).span.end};
    return make_expr<tree::IfExpr>(span, cond, then_branch, else_branch);
  }
};
} // namespace

namespace tree {
Program parse(std::vector<Token> tokens, DiagnosticEngine &diag) {
  Parser parser(std::move(tokens), diag);
  return parser.parse_program();
}
} // namespace tree