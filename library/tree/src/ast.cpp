#include "ast.hpp"
#include <algorithm>
#include <iterator>

namespace tree {

PatternPtr clone(const PatternPtr &p) { return p ? clone(*p) : nullptr; }

PatternPtr clone(const Pattern &p) {
  return match(
      p.value,
      [&](const WildcardPattern &) -> PatternPtr {
        return std::make_unique<Pattern>(
            Pattern{.value = WildcardPattern{}, .span = p.span});
      },
      [&](const VarPattern &v) -> PatternPtr {
        return std::make_unique<Pattern>(
            Pattern{.value = VarPattern{v.name}, .span = p.span});
      },
      [&](const FloatPattern &f) -> PatternPtr {
        return std::make_unique<Pattern>(
            Pattern{.value = FloatPattern{f.value}, .span = p.span});
      },
      [&](const StringPattern &s) -> PatternPtr {
        return std::make_unique<Pattern>(
            Pattern{.value = StringPattern{s.value}, .span = p.span});
      },
      [&](const TuplePattern &t) -> PatternPtr {
        std::vector<TuplePatternField> fields;
        fields.reserve(t.fields.size());
        std::ranges::transform(
            t.fields, std::back_inserter(fields), [](const auto &f) {
              return TuplePatternField{f.name, clone(f.pattern)};
            });
        return std::make_unique<Pattern>(
            Pattern{.value = TuplePattern{std::move(fields)}, .span = p.span});
      });
}

ExprPtr clone(const ExprPtr &e) { return e ? clone(*e) : nullptr; }

ExprPtr clone(const Expr &e) {
  return match(
      e.value,
      [&](const FloatLiteral &f) -> ExprPtr {
        return std::make_unique<Expr>(
            Expr{.value = FloatLiteral{f.value}, .span = e.span});
      },
      [&](const StringLiteral &s) -> ExprPtr {
        return std::make_unique<Expr>(
            Expr{.value = StringLiteral{s.value}, .span = e.span});
      },
      [&](const Identifier &id) -> ExprPtr {
        return std::make_unique<Expr>(
            Expr{.value = Identifier{id.name}, .span = e.span});
      },
      [&](const TupleExpr &t) -> ExprPtr {
        std::vector<TupleExprField> fields;
        fields.reserve(t.fields.size());
        std::ranges::transform(t.fields, std::back_inserter(fields),
                               [](const auto &f) {
                                 return TupleExprField{f.name, clone(f.value)};
                               });
        return std::make_unique<Expr>(
            Expr{.value = TupleExpr{std::move(fields)}, .span = e.span});
      },
      [&](const FieldAccess &f) -> ExprPtr {
        return std::make_unique<Expr>(
            Expr{.value = FieldAccess{.target = clone(f.target), .key = f.key},
                 .span = e.span});
      },
      [&](const Call &c) -> ExprPtr {
        std::vector<ExprPtr> args;
        args.reserve(c.args.size());
        std::ranges::transform(c.args, std::back_inserter(args),
                               [](const auto &a) { return clone(a); });
        return std::make_unique<Expr>(Expr{
            .value = Call{.callee = clone(c.callee), .args = std::move(args)},
            .span = e.span});
      },
      [&](const BinaryExpr &b) -> ExprPtr {
        return std::make_unique<Expr>(
            Expr{.value = BinaryExpr{.op = b.op,
                                     .lhs = clone(b.lhs),
                                     .rhs = clone(b.rhs)},
                 .span = e.span});
      },
      [&](const Lambda &l) -> ExprPtr {
        std::vector<PatternPtr> params;
        params.reserve(l.params.size());
        std::ranges::transform(l.params, std::back_inserter(params),
                               [](const auto &p) { return clone(p); });
        return std::make_unique<Expr>(Expr{
            .value = Lambda{.params = std::move(params), .body = clone(l.body)},
            .span = e.span});
      },
      [&](const IfExpr &i) -> ExprPtr {
        return std::make_unique<Expr>(
            Expr{.value = IfExpr{.cond = clone(i.cond),
                                 .then_branch = clone(i.then_branch),
                                 .else_branch = clone(i.else_branch)},
                 .span = e.span});
      },
      [&](const FunctionClause &f) -> ExprPtr {
        std::vector<PatternPtr> params;
        params.reserve(f.params.size());
        std::ranges::transform(f.params, std::back_inserter(params),
                               [](const auto &p) { return clone(p); });
        return std::make_unique<Expr>(
            Expr{.value = FunctionClause{.name = f.name,
                                         .params = std::move(params),
                                         .body = clone(f.body)},
                 .span = e.span});
      },
      [&](const Assignment &a) -> ExprPtr {
        return std::make_unique<Expr>(
            Expr{.value = Assignment{.target = clone(a.target),
                                     .value = clone(a.value)},
                 .span = e.span});
      });
}

Program clone(const Program &prog) {
  Program result;
  result.exprs.reserve(prog.exprs.size());
  std::ranges::transform(prog.exprs, std::back_inserter(result.exprs),
                         [](const auto &e) { return clone(e); });
  return result;
}

} // namespace tree