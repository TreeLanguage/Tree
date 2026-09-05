#pragma once

#include "span.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace tree {
struct Pattern;
using PatternPtr = std::unique_ptr<Pattern>;

struct WildcardPattern {};

struct VarPattern {
  std::string name;
};

struct FloatPattern {
  double value;
};

struct StringPattern {
  std::string value;
};

struct TuplePatternField {
  std::optional<std::string> name;
  PatternPtr pattern;
};

struct TuplePattern {
  std::vector<TuplePatternField> fields;
};

struct Pattern {
  std::variant<WildcardPattern, VarPattern, FloatPattern, StringPattern,
               TuplePattern>
      value;
  Span span;
};

template <typename T, typename... Args>
PatternPtr make_pattern(Span span, Args &&...args) {
  return std::make_unique<Pattern>(
      Pattern{T{std::forward<Args>(args)...}, span});
}

PatternPtr clone(const Pattern &p);
PatternPtr clone(const PatternPtr &p);

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct FloatLiteral {
  double value;
};

struct StringLiteral {
  std::string value;
};

struct Identifier {
  std::string name;
};

struct TupleExprField {
  std::optional<std::string> name;
  ExprPtr value;
};

struct TupleExpr {
  std::vector<TupleExprField> fields;
};

using FieldKey = std::variant<int64_t, std::string>;

struct FieldAccess {
  ExprPtr target;
  FieldKey key;
};

struct Call {
  ExprPtr callee;
  std::vector<ExprPtr> args;
};

enum class BinaryOp : uint8_t {
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Equal,
  LessThan,
};

struct BinaryExpr {
  BinaryOp op = {};
  ExprPtr lhs;
  ExprPtr rhs;
};

struct Lambda {
  std::vector<PatternPtr> params;
  ExprPtr body;
};

struct IfExpr {
  ExprPtr cond;
  ExprPtr then_branch;
  ExprPtr else_branch;
};

struct FunctionClause {
  std::string name;
  std::vector<PatternPtr> params;
  ExprPtr body;
};

struct Assignment {
  PatternPtr target;
  ExprPtr value;
};

struct Expr {
  std::variant<FloatLiteral, StringLiteral, Identifier, TupleExpr, FieldAccess,
               Call, BinaryExpr, Lambda, IfExpr, FunctionClause, Assignment>
      value;
  Span span;
};

template <typename T, typename... Args>
ExprPtr make_expr(Span span, Args &&...args) {
  return std::make_unique<Expr>(Expr{T{std::forward<Args>(args)...}, span});
}

ExprPtr clone(const Expr &e);
ExprPtr clone(const ExprPtr &e);

struct Program {
  std::vector<ExprPtr> exprs;
};

Program clone(const Program &prog);

template <typename... Fs> struct Overloaded : Fs... {
  using Fs::operator()...;
};
template <typename... Fs> Overloaded(Fs...) -> Overloaded<Fs...>;

template <typename Variant, typename... Fs>
decltype(auto) match(Variant &&v, Fs &&...fs) {
  return std::visit(Overloaded{std::forward<Fs>(fs)...},
                    std::forward<Variant>(v));
}
} // namespace tree