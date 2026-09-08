#include "ast.hpp"
#include <algorithm>
#include <iterator>

namespace tree {

PatternId Arena::clone(PatternId id) {
  const Pattern &p = get(id);
  return match(
      p.value,
      [&](const WildcardPattern &) {
        return make_pattern<WildcardPattern>(p.span);
      },
      [&](const VarPattern &v) {
        return make_pattern<VarPattern>(p.span, v.name);
      },
      [&](const FloatPattern &f) {
        return make_pattern<FloatPattern>(p.span, f.value);
      },
      [&](const StringPattern &s) {
        return make_pattern<StringPattern>(p.span, s.value);
      },
      [&](const TuplePattern &t) {
        std::vector<TuplePatternField> fields;
        fields.reserve(t.fields.size());
        std::ranges::transform(
            t.fields, std::back_inserter(fields), [&](const auto &f) {
              return TuplePatternField{.name = f.name,
                                       .pattern = clone(f.pattern)};
            });
        return make_pattern<TuplePattern>(p.span, std::move(fields));
      });
}

ExprId Arena::clone(ExprId id) {
  const Expr &e = get(id);
  return match(
      e.value,
      [&](const FloatLiteral &f) {
        return make_expr<FloatLiteral>(e.span, f.value);
      },
      [&](const StringLiteral &s) {
        return make_expr<StringLiteral>(e.span, s.value);
      },
      [&](const Identifier &i) {
        return make_expr<Identifier>(e.span, i.name);
      },
      [&](const TupleExpr &t) {
        std::vector<TupleExprField> fields;
        fields.reserve(t.fields.size());
        std::ranges::transform(
            t.fields, std::back_inserter(fields), [&](const auto &f) {
              return TupleExprField{.name = f.name, .value = clone(f.value)};
            });
        return make_expr<TupleExpr>(e.span, std::move(fields));
      },
      [&](const Call &c) {
        std::vector<ExprId> args;
        args.reserve(c.args.size());
        std::ranges::transform(c.args, std::back_inserter(args),
                               [&](auto a) { return clone(a); });
        return make_expr<Call>(e.span, clone(c.callee), std::move(args));
      },
      [&](const BinaryExpr &b) {
        return make_expr<BinaryExpr>(e.span, b.op, clone(b.lhs), clone(b.rhs));
      },
      [&](const Lambda &l) {
        std::vector<PatternId> params;
        params.reserve(l.params.size());
        std::ranges::transform(l.params, std::back_inserter(params),
                               [&](auto p) { return clone(p); });
        return make_expr<Lambda>(e.span, std::move(params), clone(l.body));
      },
      [&](const IfExpr &i) {
        return make_expr<IfExpr>(e.span, clone(i.cond), clone(i.then_branch),
                                 clone(i.else_branch));
      },
      [&](const FunctionClause &f) {
        std::vector<PatternId> params;
        params.reserve(f.params.size());
        std::ranges::transform(f.params, std::back_inserter(params),
                               [&](auto p) { return clone(p); });
        return make_expr<FunctionClause>(e.span, f.name, std::move(params),
                                         clone(f.body));
      },
      [&](const Assignment &a) {
        return make_expr<Assignment>(e.span, clone(a.target), clone(a.value));
      });
}

Program clone(const Program &prog) {
  Program result;
  result.exprs.reserve(prog.exprs.size());
  std::ranges::transform(prog.exprs, std::back_inserter(result.exprs),
                         [&](auto id) { return result.arena.clone(id); });
  return result;
}

} // namespace tree