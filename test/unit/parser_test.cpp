#include "ast.hpp"
#include "diagnostic.hpp"
#include "framework.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "token.hpp"
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

using tree::BinaryOp;
using tree::DiagnosticEngine;
using tree::Severity;

namespace {

struct ParseResult {
  tree::Program prog;
  DiagnosticEngine diag;
};

ParseResult parse_src(const std::string &src) {
  DiagnosticEngine diag("<test>", src);
  std::vector<tree::Token> tokens = tree::lexer(src, diag);
  tree::Program prog = tree::parse(std::move(tokens), diag);
  return ParseResult{.prog = std::move(prog), .diag = std::move(diag)};
}

const tree::Expr &get_expr(const tree::Program &prog, tree::ExprId id) {
  return prog.arena.get(id);
}

const tree::Pattern &get_pattern(const tree::Program &prog,
                                 tree::PatternId id) {
  return prog.arena.get(id);
}

template <typename T, typename Variant>
const T *require_alt(const Variant &v, const char *what) {
  const T *p = std::get_if<T>(&v);
  if (p == nullptr) {
    tf::report_failure(__FILE__, __LINE__,
                       std::string("expected alternative ") + what);
  }
  return p;
}

} // namespace

TEST(parses_float_literal) {
  auto r = parse_src("42");
  CHECK(!r.diag.has_errors());
  CHECK_EQ(r.prog.exprs.size(), static_cast<size_t>(1));
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *lit = require_alt<tree::FloatLiteral>(e.value, "FloatLiteral");
  if (lit != nullptr)
    CHECK_EQ(lit->value, 42.0);
}

TEST(parses_string_literal) {
  auto r = parse_src("\"hi\"");
  CHECK(!r.diag.has_errors());
  CHECK_EQ(r.prog.exprs.size(), static_cast<size_t>(1));
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *lit = require_alt<tree::StringLiteral>(e.value, "StringLiteral");
  if (lit != nullptr)
    CHECK_EQ(lit->value, std::string("hi"));
}

TEST(parses_bare_identifier) {
  auto r = parse_src("foo");
  CHECK(!r.diag.has_errors());
  CHECK_EQ(r.prog.exprs.size(), static_cast<size_t>(1));
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *id = require_alt<tree::Identifier>(e.value, "Identifier");
  if (id != nullptr)
    CHECK_EQ(id->name, std::string("foo"));
}

TEST(multiplicative_binds_tighter_than_additive) {
  auto r = parse_src("1 + 2 * 3");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *add = require_alt<tree::BinaryExpr>(e.value, "BinaryExpr");
  if (add == nullptr)
    return;
  CHECK(add->op == BinaryOp::Add);

  const tree::Expr &lhs = get_expr(r.prog, add->lhs);
  const auto *lhs_lit =
      require_alt<tree::FloatLiteral>(lhs.value, "FloatLiteral");
  if (lhs_lit != nullptr)
    CHECK_EQ(lhs_lit->value, 1.0);

  const tree::Expr &rhs = get_expr(r.prog, add->rhs);
  const auto *mul = require_alt<tree::BinaryExpr>(rhs.value, "BinaryExpr");
  if (mul != nullptr)
    CHECK(mul->op == BinaryOp::Mul);
}

TEST(comparison_is_lowest_precedence) {
  auto r = parse_src("1 + 2 == 3");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *eq = require_alt<tree::BinaryExpr>(e.value, "BinaryExpr");
  if (eq == nullptr)
    return;
  CHECK(eq->op == BinaryOp::Equal);
  const tree::Expr &lhs = get_expr(r.prog, eq->lhs);
  require_alt<tree::BinaryExpr>(lhs.value, "BinaryExpr (Add)");
}

TEST(all_multiplicative_ops) {
  {
    auto r = parse_src("6 / 2");
    const auto *b = require_alt<tree::BinaryExpr>(
        get_expr(r.prog, r.prog.exprs[0]).value, "BinaryExpr");
    if (b != nullptr)
      CHECK(b->op == BinaryOp::Div);
  }
  {
    auto r = parse_src("6 % 2");
    const auto *b = require_alt<tree::BinaryExpr>(
        get_expr(r.prog, r.prog.exprs[0]).value, "BinaryExpr");
    if (b != nullptr)
      CHECK(b->op == BinaryOp::Mod);
  }
}

TEST(comparison_less_than) {
  auto r = parse_src("1 < 2");
  const auto *b = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, r.prog.exprs[0]).value, "BinaryExpr");
  if (b != nullptr)
    CHECK(b->op == BinaryOp::LessThan);
}

TEST(unary_minus_desugars_to_zero_minus_operand) {
  auto r = parse_src("-5");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *sub = require_alt<tree::BinaryExpr>(e.value, "BinaryExpr");
  if (sub == nullptr)
    return;
  CHECK(sub->op == BinaryOp::Sub);
  const tree::Expr &lhs = get_expr(r.prog, sub->lhs);
  const auto *zero = require_alt<tree::FloatLiteral>(lhs.value, "FloatLiteral");
  if (zero != nullptr)
    CHECK_EQ(zero->value, 0.0);
  const tree::Expr &rhs = get_expr(r.prog, sub->rhs);
  const auto *five = require_alt<tree::FloatLiteral>(rhs.value, "FloatLiteral");
  if (five != nullptr)
    CHECK_EQ(five->value, 5.0);
}

TEST(single_parenthesized_expr_unwraps_not_a_tuple) {
  auto r = parse_src("(42)");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  require_alt<tree::FloatLiteral>(e.value, "FloatLiteral");
}

TEST(empty_tuple_is_zero_field_tuple_expr) {
  auto r = parse_src("()");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *tup = require_alt<tree::TupleExpr>(e.value, "TupleExpr");
  if (tup != nullptr)
    CHECK_EQ(tup->fields.size(), static_cast<size_t>(0));
}

TEST(two_element_tuple_construction) {
  auto r = parse_src("(1, 2)");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *tup = require_alt<tree::TupleExpr>(e.value, "TupleExpr");
  if (tup == nullptr)
    return;
  CHECK_EQ(tup->fields.size(), static_cast<size_t>(2));
  CHECK(!tup->fields[0].name.has_value());
  CHECK(!tup->fields[1].name.has_value());
}

TEST(trailing_comma_free_single_element_still_unwraps) {
  auto r = parse_src("(foo)");
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  require_alt<tree::Identifier>(e.value, "Identifier");
}

TEST(named_tuple_fields) {
  auto r = parse_src("(x: 10, y: 20)");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *tup = require_alt<tree::TupleExpr>(e.value, "TupleExpr");
  if (tup == nullptr)
    return;
  CHECK_EQ(tup->fields.size(), static_cast<size_t>(2));
  CHECK(tup->fields[0].name.has_value());
  if (tup->fields[0].name)
    CHECK_EQ(*tup->fields[0].name, std::string("x"));
  CHECK(tup->fields[1].name.has_value());
  if (tup->fields[1].name)
    CHECK_EQ(*tup->fields[1].name, std::string("y"));
}

TEST(function_call_with_args) {
  auto r = parse_src("add(1, 2)");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *call = require_alt<tree::Call>(e.value, "Call");
  if (call == nullptr)
    return;
  const tree::Expr &callee = get_expr(r.prog, call->callee);
  const auto *id = require_alt<tree::Identifier>(callee.value, "Identifier");
  if (id != nullptr)
    CHECK_EQ(id->name, std::string("add"));
  CHECK_EQ(call->args.size(), static_cast<size_t>(2));
}

TEST(chained_calls_are_nested) {
  auto r = parse_src("f(1)(2)");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *outer = require_alt<tree::Call>(e.value, "Call");
  if (outer == nullptr)
    return;
  CHECK_EQ(outer->args.size(), static_cast<size_t>(1));
  const tree::Expr &inner_expr = get_expr(r.prog, outer->callee);
  const auto *inner = require_alt<tree::Call>(inner_expr.value, "Call");
  if (inner != nullptr)
    CHECK_EQ(inner->args.size(), static_cast<size_t>(1));
}

TEST(call_must_be_on_same_line_as_callee) {
  auto r = parse_src("f\n(1)");
  CHECK(!r.diag.has_errors());
  CHECK_EQ(r.prog.exprs.size(), static_cast<size_t>(2));
  const tree::Expr &first = get_expr(r.prog, r.prog.exprs[0]);
  require_alt<tree::Identifier>(first.value, "Identifier");
  const tree::Expr &second = get_expr(r.prog, r.prog.exprs[1]);
  require_alt<tree::FloatLiteral>(second.value, "FloatLiteral");
}

TEST(lambda_single_param) {
  auto r = parse_src("\\x -> x");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *lam = require_alt<tree::Lambda>(e.value, "Lambda");
  if (lam == nullptr)
    return;
  const auto *p = require_alt<tree::VarPattern>(
      get_pattern(r.prog, lam->param).value, "VarPattern");
  if (p != nullptr)
    CHECK_EQ(p->name, std::string("x"));
}

TEST(lambda_multiple_params_in_parens) {
  auto r = parse_src("\\(x, y) -> x + y");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *lam = require_alt<tree::Lambda>(e.value, "Lambda");
  if (lam == nullptr)
    return;
  const auto *tup_pat = require_alt<tree::TuplePattern>(
      get_pattern(r.prog, lam->param).value, "TuplePattern");
  if (tup_pat != nullptr)
    CHECK_EQ(tup_pat->fields.size(), static_cast<size_t>(2));
  const tree::Expr &body = get_expr(r.prog, lam->body);
  require_alt<tree::BinaryExpr>(body.value, "BinaryExpr");
}

TEST(if_then_else) {
  auto r = parse_src("if x then 1 else 2");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *iff = require_alt<tree::IfExpr>(e.value, "IfExpr");
  if (iff == nullptr)
    return;
  require_alt<tree::Identifier>(get_expr(r.prog, iff->cond).value,
                                "Identifier");
  const auto *then_lit = require_alt<tree::FloatLiteral>(
      get_expr(r.prog, iff->then_branch).value, "FloatLiteral");
  if (then_lit != nullptr)
    CHECK_EQ(then_lit->value, 1.0);
  const auto *else_lit = require_alt<tree::FloatLiteral>(
      get_expr(r.prog, iff->else_branch).value, "FloatLiteral");
  if (else_lit != nullptr)
    CHECK_EQ(else_lit->value, 2.0);
}

TEST(simple_Binding_to_var_pattern) {
  auto r = parse_src("x = 5");
  CHECK(!r.diag.has_errors());
  CHECK_EQ(r.prog.exprs.size(), static_cast<size_t>(1));
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *assign = require_alt<tree::Binding>(e.value, "Binding");
  if (assign == nullptr)
    return;
  const auto *pat = require_alt<tree::VarPattern>(
      get_pattern(r.prog, assign->target).value, "VarPattern");
  if (pat != nullptr)
    CHECK_EQ(pat->name, std::string("x"));
}

TEST(tuple_destructuring_Binding) {
  auto r = parse_src("(a, b) = pair");
  CHECK(!r.diag.has_errors());
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *assign = require_alt<tree::Binding>(e.value, "Binding");
  if (assign == nullptr)
    return;
  const auto *pat = require_alt<tree::TuplePattern>(
      get_pattern(r.prog, assign->target).value, "TuplePattern");
  if (pat == nullptr)
    return;
  CHECK_EQ(pat->fields.size(), static_cast<size_t>(2));
}

TEST(wildcard_and_var_patterns_in_tuple_destructure) {
  auto r = parse_src("(_, _, third) = triple");
  CHECK(!r.diag.has_errors());
  const auto *assign = require_alt<tree::Binding>(
      get_expr(r.prog, r.prog.exprs[0]).value, "Binding");
  if (assign == nullptr)
    return;
  const auto *pat = require_alt<tree::TuplePattern>(
      get_pattern(r.prog, assign->target).value, "TuplePattern");
  if (pat == nullptr)
    return;
  CHECK_EQ(pat->fields.size(), static_cast<size_t>(3));
  require_alt<tree::WildcardPattern>(
      get_pattern(r.prog, pat->fields[0].pattern).value, "WildcardPattern");
  require_alt<tree::WildcardPattern>(
      get_pattern(r.prog, pat->fields[1].pattern).value, "WildcardPattern");
  const auto *third = require_alt<tree::VarPattern>(
      get_pattern(r.prog, pat->fields[2].pattern).value, "VarPattern");
  if (third != nullptr)
    CHECK_EQ(third->name, std::string("third"));
}

TEST(named_field_pattern_with_catchall_wildcard) {
  auto r = parse_src("(x: px, _) = point");
  CHECK(!r.diag.has_errors());
  const auto *assign = require_alt<tree::Binding>(
      get_expr(r.prog, r.prog.exprs[0]).value, "Binding");
  if (assign == nullptr)
    return;
  const auto *pat = require_alt<tree::TuplePattern>(
      get_pattern(r.prog, assign->target).value, "TuplePattern");
  if (pat == nullptr)
    return;
  CHECK_EQ(pat->fields.size(), static_cast<size_t>(2));
  CHECK(pat->fields[0].name.has_value());
  if (pat->fields[0].name)
    CHECK_EQ(*pat->fields[0].name, std::string("x"));
  const auto *px = require_alt<tree::VarPattern>(
      get_pattern(r.prog, pat->fields[0].pattern).value, "VarPattern");
  if (px != nullptr)
    CHECK_EQ(px->name, std::string("px"));
  CHECK(!pat->fields[1].name.has_value());
  require_alt<tree::WildcardPattern>(
      get_pattern(r.prog, pat->fields[1].pattern).value, "WildcardPattern");
}

TEST(function_clause_with_tuple_param_pattern) {
  auto r = parse_src("add((x, y)) = x + y");
  CHECK(!r.diag.has_errors());
  CHECK_EQ(r.prog.exprs.size(), static_cast<size_t>(1));
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *assign = require_alt<tree::Binding>(e.value, "Binding");
  if (assign == nullptr)
    return;
  const auto *target = require_alt<tree::VarPattern>(
      get_pattern(r.prog, assign->target).value, "VarPattern");
  if (target != nullptr)
    CHECK_EQ(target->name, std::string("add"));
  const auto *lambda = require_alt<tree::Lambda>(
      get_expr(r.prog, assign->value).value, "Lambda");
  if (lambda == nullptr)
    return;
  CHECK(lambda->name.has_value());
  CHECK_EQ(*lambda->name, std::string("add"));
  const auto *tup_pat = require_alt<tree::TuplePattern>(
      get_pattern(r.prog, lambda->param).value, "TuplePattern");
  if (tup_pat != nullptr)
    CHECK_EQ(tup_pat->fields.size(), static_cast<size_t>(2));
  const tree::Expr &body = get_expr(r.prog, lambda->body);
  require_alt<tree::BinaryExpr>(body.value, "BinaryExpr");
}

TEST(function_clause_with_literal_pattern) {
  auto r = parse_src("factorial(0) = 1");
  CHECK(!r.diag.has_errors());
  const auto *assign = require_alt<tree::Binding>(
      get_expr(r.prog, r.prog.exprs[0]).value, "Binding");
  if (assign == nullptr)
    return;
  const auto *target = require_alt<tree::VarPattern>(
      get_pattern(r.prog, assign->target).value, "VarPattern");
  if (target != nullptr)
    CHECK_EQ(target->name, std::string("factorial"));
  const auto *lambda = require_alt<tree::Lambda>(
      get_expr(r.prog, assign->value).value, "Lambda");
  if (lambda == nullptr)
    return;
  CHECK(lambda->name.has_value());
  CHECK_EQ(*lambda->name, std::string("factorial"));
  const auto *lit_pat = require_alt<tree::FloatPattern>(
      get_pattern(r.prog, lambda->param).value, "FloatPattern");
  if (lit_pat != nullptr)
    CHECK_EQ(lit_pat->value, 0.0);
}

TEST(function_clause_with_nested_tuple_pattern) {
  auto r = parse_src("fibAcc((n, (a, b))) = a");
  CHECK(!r.diag.has_errors());
  CHECK_EQ(r.prog.exprs.size(), static_cast<size_t>(1));
  const tree::Expr &e = get_expr(r.prog, r.prog.exprs[0]);
  const auto *assign = require_alt<tree::Binding>(e.value, "Binding");
  if (assign == nullptr)
    return;
  const auto *target = require_alt<tree::VarPattern>(
      get_pattern(r.prog, assign->target).value, "VarPattern");
  if (target != nullptr)
    CHECK_EQ(target->name, std::string("fibAcc"));
  const auto *lambda = require_alt<tree::Lambda>(
      get_expr(r.prog, assign->value).value, "Lambda");
  if (lambda == nullptr)
    return;
  CHECK(lambda->name.has_value());
  CHECK_EQ(*lambda->name, std::string("fibAcc"));
  const auto *tup_pat = require_alt<tree::TuplePattern>(
      get_pattern(r.prog, lambda->param).value, "TuplePattern");
  if (tup_pat == nullptr)
    return;
  CHECK_EQ(tup_pat->fields.size(), static_cast<size_t>(2));
  const auto *first = require_alt<tree::VarPattern>(
      get_pattern(r.prog, tup_pat->fields[0].pattern).value, "VarPattern");
  (void)first;
  const auto *nested = require_alt<tree::TuplePattern>(
      get_pattern(r.prog, tup_pat->fields[1].pattern).value, "TuplePattern");
  if (nested == nullptr)
    return;
  CHECK_EQ(nested->fields.size(), static_cast<size_t>(2));
}

TEST(multiple_top_level_statements) {
  auto r = parse_src("x = 1\ny = 2");
  CHECK(!r.diag.has_errors());
  CHECK_EQ(r.prog.exprs.size(), static_cast<size_t>(2));
  require_alt<tree::Binding>(get_expr(r.prog, r.prog.exprs[0]).value,
                             "Binding");
  require_alt<tree::Binding>(get_expr(r.prog, r.prog.exprs[1]).value,
                             "Binding");
}

TEST(unclosed_paren_reports_error) {
  auto r = parse_src("(1, 2");
  CHECK(r.diag.has_errors());
  CHECK(r.diag.count(Severity::Error) >= static_cast<size_t>(1));
}

TEST(invalid_Binding_target_reports_error) {
  auto r = parse_src("(1)(2) = 3");
  CHECK(r.diag.has_errors());
}

TEST(parse_error_does_not_stop_subsequent_statements) {
  auto r = parse_src("1 + 2 = 3\nfoo");
  CHECK(r.diag.has_errors());
  bool found_foo = false;
  for (const tree::ExprId id : r.prog.exprs) {
    const tree::Expr &e = get_expr(r.prog, id);
    if (const auto *ident = std::get_if<tree::Identifier>(&e.value)) {
      if (ident->name == "foo") {
        found_foo = true;
      }
    }
  }
  CHECK(found_foo);
}

TEST(deep_nested_named_tuple_destructure) {
  auto r = parse_src("(name: n, address: (city: c, _), _) = user");
  CHECK(!r.diag.has_errors());
  const auto *assign = require_alt<tree::Binding>(
      get_expr(r.prog, r.prog.exprs[0]).value, "Binding");
  if (assign == nullptr)
    return;
  const auto *pat = require_alt<tree::TuplePattern>(
      get_pattern(r.prog, assign->target).value, "TuplePattern");
  if (pat == nullptr)
    return;
  CHECK_EQ(pat->fields.size(), static_cast<size_t>(3));

  CHECK(pat->fields[0].name.has_value());
  if (pat->fields[0].name)
    CHECK_EQ(*pat->fields[0].name, std::string("name"));

  CHECK(pat->fields[1].name.has_value());
  if (pat->fields[1].name)
    CHECK_EQ(*pat->fields[1].name, std::string("address"));
  const auto *nested = require_alt<tree::TuplePattern>(
      get_pattern(r.prog, pat->fields[1].pattern).value, "TuplePattern");
  if (nested != nullptr)
    CHECK_EQ(nested->fields.size(), static_cast<size_t>(2));

  CHECK(!pat->fields[2].name.has_value());
  require_alt<tree::WildcardPattern>(
      get_pattern(r.prog, pat->fields[2].pattern).value, "WildcardPattern");
}

TEST(recursive_style_clause_sum_list) {
  auto r = parse_src("sumList((x, rest)) = x + sumList(rest)");
  CHECK(!r.diag.has_errors());
  const auto *assign = require_alt<tree::Binding>(
      get_expr(r.prog, r.prog.exprs[0]).value, "Binding");
  if (assign == nullptr)
    return;
  const auto *target = require_alt<tree::VarPattern>(
      get_pattern(r.prog, assign->target).value, "VarPattern");
  if (target != nullptr)
    CHECK_EQ(target->name, std::string("sumList"));
  const auto *lambda = require_alt<tree::Lambda>(
      get_expr(r.prog, assign->value).value, "Lambda");
  if (lambda == nullptr)
    return;
  CHECK(lambda->name.has_value());
  CHECK_EQ(*lambda->name, std::string("sumList"));
  const tree::Expr &body = get_expr(r.prog, lambda->body);
  const auto *add = require_alt<tree::BinaryExpr>(body.value, "BinaryExpr");
  if (add != nullptr)
    CHECK(add->op == tree::BinaryOp::Add);
}

TEST(unary_minus_binds_tighter_than_multiplication) {
  auto r = parse_src("-2 * 3");
  CHECK(!r.diag.has_errors());
  const auto *mul = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, r.prog.exprs[0]).value, "BinaryExpr");
  if (mul == nullptr)
    return;
  CHECK(mul->op == BinaryOp::Mul);
  const auto *sub = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, mul->lhs).value, "BinaryExpr");
  if (sub != nullptr)
    CHECK(sub->op == BinaryOp::Sub);
}

TEST(unary_minus_applies_to_entire_postfix_expression) {
  auto r = parse_src("-f(1)");
  CHECK(!r.diag.has_errors());
  const auto *sub = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, r.prog.exprs[0]).value, "BinaryExpr");
  if (sub == nullptr)
    return;
  CHECK(sub->op == BinaryOp::Sub);
  const auto *call =
      require_alt<tree::Call>(get_expr(r.prog, sub->rhs).value, "Call");
  CHECK(call != nullptr);
}

TEST(chained_unary_minus_nests) {
  auto r = parse_src("---5");
  CHECK(!r.diag.has_errors());
  const auto *outer = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, r.prog.exprs[0]).value, "BinaryExpr");
  CHECK(outer != nullptr);
  if (outer == nullptr)
    return;
  CHECK(outer->op == BinaryOp::Sub);
  const auto *middle = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, outer->rhs).value, "BinaryExpr");
  CHECK(middle != nullptr);
  if (middle == nullptr)
    return;
  CHECK(middle->op == BinaryOp::Sub);
}

TEST(full_operator_precedence_chain) {
  auto r = parse_src("1 + 2 * 3 == 7");
  CHECK(!r.diag.has_errors());
  const auto *eq = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, r.prog.exprs[0]).value, "BinaryExpr");
  if (eq == nullptr)
    return;
  CHECK(eq->op == BinaryOp::Equal);
  const auto *add = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, eq->lhs).value, "BinaryExpr");
  if (add == nullptr)
    return;
  CHECK(add->op == BinaryOp::Add);
  const auto *mul = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, add->rhs).value, "BinaryExpr");
  if (mul != nullptr)
    CHECK(mul->op == BinaryOp::Mul);
}

TEST(parentheses_override_precedence) {
  auto r = parse_src("(1 + 2) * 3");
  CHECK(!r.diag.has_errors());
  const auto *mul = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, r.prog.exprs[0]).value, "BinaryExpr");
  if (mul == nullptr)
    return;
  CHECK(mul->op == BinaryOp::Mul);
  const auto *add = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, mul->lhs).value, "BinaryExpr");
  if (add != nullptr)
    CHECK(add->op == BinaryOp::Add);
}

TEST(call_with_no_arguments) {
  auto r = parse_src("f()");
  CHECK(!r.diag.has_errors());
  const auto *call =
      require_alt<tree::Call>(get_expr(r.prog, r.prog.exprs[0]).value, "Call");
  if (call != nullptr)
    CHECK_EQ(call->args.size(), static_cast<size_t>(0));
}

TEST(call_with_one_argument) {
  auto r = parse_src("f(42)");
  CHECK(!r.diag.has_errors());
  const auto *call =
      require_alt<tree::Call>(get_expr(r.prog, r.prog.exprs[0]).value, "Call");
  if (call != nullptr)
    CHECK_EQ(call->args.size(), static_cast<size_t>(1));
}

TEST(call_on_parenthesized_expression) {
  auto r = parse_src("(f)(1)");
  CHECK(!r.diag.has_errors());
  const auto *call =
      require_alt<tree::Call>(get_expr(r.prog, r.prog.exprs[0]).value, "Call");
  CHECK(call != nullptr);
}

TEST(chained_calls_preserve_left_associativity) {
  auto r = parse_src("f(1)(2)(3)");
  CHECK(!r.diag.has_errors());
  const auto *outer =
      require_alt<tree::Call>(get_expr(r.prog, r.prog.exprs[0]).value, "Call");
  CHECK(outer != nullptr);
  if (outer == nullptr)
    return;
  const auto *middle =
      require_alt<tree::Call>(get_expr(r.prog, outer->callee).value, "Call");
  CHECK(middle != nullptr);
  if (middle == nullptr)
    return;
  const auto *inner =
      require_alt<tree::Call>(get_expr(r.prog, middle->callee).value, "Call");
  CHECK(inner != nullptr);
}

TEST(lambda_body_includes_full_expression) {
  auto r = parse_src("\\x -> x + 1 * 2");
  CHECK(!r.diag.has_errors());
  const auto *lam = require_alt<tree::Lambda>(
      get_expr(r.prog, r.prog.exprs[0]).value, "Lambda");
  if (lam == nullptr)
    return;
  const auto *add = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, lam->body).value, "BinaryExpr");
  if (add == nullptr)
    return;
  CHECK(add->op == BinaryOp::Add);
  const auto *mul = require_alt<tree::BinaryExpr>(
      get_expr(r.prog, add->rhs).value, "BinaryExpr");
  if (mul != nullptr)
    CHECK(mul->op == BinaryOp::Mul);
}

TEST(nested_lambdas) {
  auto r = parse_src("\\x -> \\y -> x + y");
  CHECK(!r.diag.has_errors());
  const auto *outer = require_alt<tree::Lambda>(
      get_expr(r.prog, r.prog.exprs[0]).value, "Lambda");
  if (outer == nullptr)
    return;
  const auto *inner =
      require_alt<tree::Lambda>(get_expr(r.prog, outer->body).value, "Lambda");
  CHECK(inner != nullptr);
}

TEST(nested_if_expression) {
  auto r = parse_src("if a then if b then 1 else 2 else 3");
  CHECK(!r.diag.has_errors());
  const auto *outer = require_alt<tree::IfExpr>(
      get_expr(r.prog, r.prog.exprs[0]).value, "IfExpr");
  if (outer == nullptr)
    return;
  const auto *inner = require_alt<tree::IfExpr>(
      get_expr(r.prog, outer->then_branch).value, "IfExpr");
  CHECK(inner != nullptr);
}

TEST(wildcard_pattern_binding) {
  auto r = parse_src("_ = value");
  CHECK(!r.diag.has_errors());
  const auto *binding = require_alt<tree::Binding>(
      get_expr(r.prog, r.prog.exprs[0]).value, "Binding");
  if (binding == nullptr)
    return;
  require_alt<tree::WildcardPattern>(get_pattern(r.prog, binding->target).value,
                                     "WildcardPattern");
}

TEST(mixed_nested_patterns) {
  auto r = parse_src("(a, (b, _), c: d) = value");
  CHECK(!r.diag.has_errors());
  const auto *binding = require_alt<tree::Binding>(
      get_expr(r.prog, r.prog.exprs[0]).value, "Binding");
  CHECK(binding != nullptr);
}