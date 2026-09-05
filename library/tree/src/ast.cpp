#include "ast.hpp"
#include <algorithm>
#include <iterator>

namespace tree {

PatternPtr clone(const PatternPtr &p) { return p ? clone(*p) : nullptr; }

PatternPtr clone(const Pattern &p) {
  return match(
      p.value,
      [&](const WildcardPattern &) -> PatternPtr {
        return std::make_unique<Pattern>(Pattern{WildcardPattern{}, p.span});
      },
      [&](const VarPattern &v) -> PatternPtr {
        return std::make_unique<Pattern>(Pattern{VarPattern{v.name}, p.span});
      },
      [&](const FloatPattern &f) -> PatternPtr {
        return std::make_unique<Pattern>(
            Pattern{FloatPattern{f.value}, p.span});
      },
      [&](const StringPattern &s) -> PatternPtr {
        return std::make_unique<Pattern>(
            Pattern{StringPattern{s.value}, p.span});
      },
      [&](const TuplePattern &t) -> PatternPtr {
        std::vector<TuplePatternField> fields;
        fields.reserve(t.fields.size());
        std::transform(t.fields.begin(), t.fields.end(),
                       std::back_inserter(fields), [](const auto &f) {
                         return TuplePatternField{f.name, clone(f.pattern)};
                       });
        return std::make_unique<Pattern>(
            Pattern{TuplePattern{std::move(fields)}, p.span});
      });
}

ExprPtr clone(const ExprPtr &e) { return e ? clone(*e) : nullptr; }

ExprPtr clone(const Expr &e) {
  return match(
      e.value,
      [&](const FloatLiteral &f) -> ExprPtr {
        return std::make_unique<Expr>(Expr{FloatLiteral{f.value}, e.span});
      },
      [&](const StringLiteral &s) -> ExprPtr {
        return std::make_unique<Expr>(Expr{StringLiteral{s.value}, e.span});
      },
      [&](const Identifier &id) -> ExprPtr {
        return std::make_unique<Expr>(Expr{Identifier{id.name}, e.span});
      },
      [&](const TupleExpr &t) -> ExprPtr {
        std::vector<TupleExprField> fields;
        fields.reserve(t.fields.size());
        std::transform(t.fields.begin(), t.fields.end(),
                       std::back_inserter(fields), [](const auto &f) {
                         return TupleExprField{f.name, clone(f.value)};
                       });
        return std::make_unique<Expr>(
            Expr{TupleExpr{std::move(fields)}, e.span});
      },
      [&](const FieldAccess &f) -> ExprPtr {
        return std::make_unique<Expr>(
            Expr{FieldAccess{clone(f.target), f.key}, e.span});
      },
      [&](const Call &c) -> ExprPtr {
        std::vector<ExprPtr> args;
        args.reserve(c.args.size());
        std::transform(c.args.begin(), c.args.end(), std::back_inserter(args),
                       [](const auto &a) { return clone(a); });
        return std::make_unique<Expr>(
            Expr{Call{clone(c.callee), std::move(args)}, e.span});
      },
      [&](const BinaryExpr &b) -> ExprPtr {
        return std::make_unique<Expr>(
            Expr{BinaryExpr{b.op, clone(b.lhs), clone(b.rhs)}, e.span});
      },
      [&](const Lambda &l) -> ExprPtr {
        std::vector<PatternPtr> params;
        params.reserve(l.params.size());
        std::transform(l.params.begin(), l.params.end(),
                       std::back_inserter(params),
                       [](const auto &p) { return clone(p); });
        return std::make_unique<Expr>(
            Expr{Lambda{std::move(params), clone(l.body)}, e.span});
      },
      [&](const IfExpr &i) -> ExprPtr {
        return std::make_unique<Expr>(Expr{
            IfExpr{clone(i.cond), clone(i.then_branch), clone(i.else_branch)},
            e.span});
      },
      [&](const FunctionClause &f) -> ExprPtr {
        std::vector<PatternPtr> params;
        params.reserve(f.params.size());
        std::transform(f.params.begin(), f.params.end(),
                       std::back_inserter(params),
                       [](const auto &p) { return clone(p); });
        return std::make_unique<Expr>(Expr{
            FunctionClause{f.name, std::move(params), clone(f.body)}, e.span});
      },
      [&](const Assignment &a) -> ExprPtr {
        return std::make_unique<Expr>(
            Expr{Assignment{clone(a.target), clone(a.value)}, e.span});
      });
}

Program clone(const Program &prog) {
  Program result;
  result.exprs.reserve(prog.exprs.size());
  std::transform(prog.exprs.begin(), prog.exprs.end(),
                 std::back_inserter(result.exprs),
                 [](const auto &e) { return clone(e); });
  return result;
}

} // namespace tree