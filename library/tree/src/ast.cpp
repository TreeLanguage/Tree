#include "ast.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

void append_line(std::string &out, const std::string &prefix, bool is_last,
                 const std::string &label) {
  out += prefix;
  out += is_last ? "└─ " : "├─ ";
  out += label;
  out += '\n';
}

std::string child_prefix(const std::string &prefix, bool is_last) {
  return prefix + (is_last ? "   " : "│  ");
}

const char *binary_op_string(tree::BinaryOp op) {
  switch (op) {
  case tree::BinaryOp::Add:
    return "+";
  case tree::BinaryOp::Sub:
    return "-";
  case tree::BinaryOp::Mul:
    return "*";
  case tree::BinaryOp::Div:
    return "/";
  case tree::BinaryOp::Mod:
    return "%";
  case tree::BinaryOp::Equal:
    return "==";
  case tree::BinaryOp::LessThan:
    return "<";
  }
  return "?";
}

std::string format_number(double value) {
  if (std::trunc(value) == value && std::abs(value) < 1e15) {
    return std::to_string(static_cast<long long>(value));
  }
  return std::to_string(value);
}

void print_pattern(const tree::Arena &arena, tree::PatternId id,
                   std::string &out, const std::string &prefix, bool is_last);

void print_pattern_fields(const tree::Arena &arena,
                          const std::vector<tree::TuplePatternField> &fields,
                          std::string &out, const std::string &prefix) {
  for (size_t i = 0; i < fields.size(); ++i) {
    const bool last = (i + 1 == fields.size());
    const auto &f = fields[i];
    const std::string label = f.name.has_value() ? (*f.name + ":") : "field";
    append_line(out, prefix, last, label);
    print_pattern(arena, f.pattern, out, child_prefix(prefix, last), true);
  }
}

void print_pattern(const tree::Arena &arena, tree::PatternId id,
                   std::string &out, const std::string &prefix, bool is_last) {
  const tree::Pattern &p = arena.get(id);
  match(
      p.value,
      [&](const tree::WildcardPattern &) {
        append_line(out, prefix, is_last, "_");
      },
      [&](const tree::VarPattern &v) {
        append_line(out, prefix, is_last, "Var(" + v.name + ")");
      },
      [&](const tree::FloatPattern &f) {
        append_line(out, prefix, is_last,
                    "Float(" + format_number(f.value) + ")");
      },
      [&](const tree::StringPattern &s) {
        append_line(out, prefix, is_last, "String(\"" + s.value + "\")");
      },
      [&](const tree::BoolPattern &b) {
        append_line(out, prefix, is_last,
                    std::string("Bool(") + (b.value ? "true" : "false") + ")");
      },
      [&](const tree::TuplePattern &t) {
        append_line(out, prefix, is_last, "Tuple");
        print_pattern_fields(arena, t.fields, out,
                             child_prefix(prefix, is_last));
      });
}

void print_expr(const tree::Arena &arena, tree::ExprId id, std::string &out,
                const std::string &prefix, bool is_last);

void print_expr_fields(const tree::Arena &arena,
                       const std::vector<tree::TupleExprField> &fields,
                       std::string &out, const std::string &prefix) {
  for (size_t i = 0; i < fields.size(); ++i) {
    const bool last = (i + 1 == fields.size());
    const auto &f = fields[i];
    const std::string label = f.name.has_value() ? (*f.name + ":") : "field";
    append_line(out, prefix, last, label);
    print_expr(arena, f.value, out, child_prefix(prefix, last), true);
  }
}

void print_expr(const tree::Arena &arena, tree::ExprId id, std::string &out,
                const std::string &prefix, bool is_last) {
  const tree::Expr &e = arena.get(id);
  match(
      e.value,
      [&](const tree::FloatLiteral &f) {
        append_line(out, prefix, is_last,
                    "Float(" + format_number(f.value) + ")");
      },
      [&](const tree::StringLiteral &s) {
        append_line(out, prefix, is_last, "String(\"" + s.value + "\")");
      },
      [&](const tree::BoolLiteral &b) {
        append_line(out, prefix, is_last,
                    std::string("Bool(") + (b.value ? "true" : "false") + ")");
      },
      [&](const tree::Identifier &i) {
        append_line(out, prefix, is_last, "Ident(" + i.name + ")");
      },
      [&](const tree::TupleExpr &t) {
        append_line(out, prefix, is_last, "Tuple");
        print_expr_fields(arena, t.fields, out, child_prefix(prefix, is_last));
      },
      [&](const tree::Call &c) {
        append_line(out, prefix, is_last, "Call");
        const std::string cp = child_prefix(prefix, is_last);
        append_line(out, cp, false, "callee:");
        print_expr(arena, c.callee, out, child_prefix(cp, false), true);
        append_line(out, cp, true, "arg:");
        print_expr(arena, c.arg, out, child_prefix(cp, true), true);
      },
      [&](const tree::BinaryExpr &b) {
        append_line(out, prefix, is_last,
                    std::string("BinaryExpr(") + binary_op_string(b.op) + ")");
        const std::string cp = child_prefix(prefix, is_last);
        print_expr(arena, b.lhs, out, cp, false);
        print_expr(arena, b.rhs, out, cp, true);
      },
      [&](const tree::Lambda &l) {
        std::string label = "Lambda";
        if (l.name.has_value()) {
          label += "(" + *l.name + ")";
        }
        append_line(out, prefix, is_last, label);
        const std::string cp = child_prefix(prefix, is_last);
        append_line(out, cp, false, "param:");
        print_pattern(arena, l.param, out, child_prefix(cp, false), true);
        append_line(out, cp, true, "body:");
        print_expr(arena, l.body, out, child_prefix(cp, true), true);
      },
      [&](const tree::Binding &a) {
        append_line(out, prefix, is_last, "Binding");
        const std::string cp = child_prefix(prefix, is_last);
        append_line(out, cp, false, "target:");
        print_pattern(arena, a.target, out, child_prefix(cp, false), true);
        append_line(out, cp, true, "value:");
        print_expr(arena, a.value, out, child_prefix(cp, true), true);
      },
      [&](const tree::MultiClauseLambda &m) {
        std::string label = "MultiClauseLambda";
        if (m.name.has_value()) {
          label += "(" + *m.name + ")";
        }
        append_line(out, prefix, is_last, label);
        const std::string cp = child_prefix(prefix, is_last);
        for (size_t i = 0; i < m.clauses.size(); ++i) {
          const bool clause_last = (i + 1 == m.clauses.size());
          append_line(out, cp, clause_last,
                      "clause " + std::to_string(i) + ":");
          const std::string clause_prefix = child_prefix(cp, clause_last);
          append_line(out, clause_prefix, false, "param:");
          print_pattern(arena, m.clauses[i].param, out,
                        child_prefix(clause_prefix, false), true);
          append_line(out, clause_prefix, true, "body:");
          print_expr(arena, m.clauses[i].body, out,
                     child_prefix(clause_prefix, true), true);
        }
      });
}

} // namespace

namespace tree {

PatternId Arena::clone(const Arena &src, PatternId id) {
  const Pattern &p = src.get(id);
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
      [&](const BoolPattern &b) {
        return make_pattern<BoolPattern>(p.span, b.value);
      },
      [&](const TuplePattern &t) {
        std::vector<TuplePatternField> fields;
        fields.reserve(t.fields.size());
        std::ranges::transform(
            t.fields, std::back_inserter(fields), [&](const auto &f) {
              return TuplePatternField{.name = f.name,
                                       .pattern = clone(src, f.pattern)};
            });
        return make_pattern<TuplePattern>(p.span, std::move(fields));
      });
}

ExprId Arena::clone(const Arena &src, ExprId id) {
  const Expr &e = src.get(id);
  return match(
      e.value,
      [&](const FloatLiteral &f) {
        return make_expr<FloatLiteral>(e.span, f.value);
      },
      [&](const StringLiteral &s) {
        return make_expr<StringLiteral>(e.span, s.value);
      },
      [&](const BoolLiteral &b) {
        return make_expr<BoolLiteral>(e.span, b.value);
      },
      [&](const Identifier &i) {
        return make_expr<Identifier>(e.span, i.name);
      },
      [&](const TupleExpr &t) {
        std::vector<TupleExprField> fields;
        fields.reserve(t.fields.size());
        std::ranges::transform(
            t.fields, std::back_inserter(fields), [&](const auto &f) {
              return TupleExprField{.name = f.name,
                                    .value = clone(src, f.value)};
            });
        return make_expr<TupleExpr>(e.span, std::move(fields));
      },
      [&](const Call &c) {
        return make_expr<Call>(e.span, clone(src, c.callee), clone(src, c.arg));
      },
      [&](const BinaryExpr &b) {
        return make_expr<BinaryExpr>(e.span, b.op, clone(src, b.lhs),
                                     clone(src, b.rhs));
      },
      [&](const Lambda &l) {
        return make_expr<Lambda>(e.span, std::optional<std::string>(l.name),
                                 clone(src, l.param), clone(src, l.body));
      },
      [&](const Binding &a) {
        return make_expr<Binding>(e.span, clone(src, a.target),
                                  clone(src, a.value));
      },
      [&](const MultiClauseLambda &m) {
        std::vector<LambdaClause> clauses;
        clauses.reserve(m.clauses.size());
        std::ranges::transform(
            m.clauses, std::back_inserter(clauses), [&](const auto &c) {
              return LambdaClause{.param = clone(src, c.param),
                                  .body = clone(src, c.body)};
            });
        return make_expr<MultiClauseLambda>(
            e.span, std::optional<std::string>(m.name), std::move(clauses));
      });
}

Program clone(const Program &prog) {
  Program result;
  result.exprs.reserve(prog.exprs.size());
  std::ranges::transform(
      prog.exprs, std::back_inserter(result.exprs),
      [&](ExprId id) { return result.arena.clone(prog.arena, id); });
  return result;
}

std::string to_string(const Arena &arena, ExprId id) {
  std::string out;
  print_expr(arena, id, out, "", true);
  return out;
}

std::string to_string(const Arena &arena, PatternId id) {
  std::string out;
  print_pattern(arena, id, out, "", true);
  return out;
}

std::string to_string(const Program &prog) {
  std::string out;
  out += "Program\n";
  for (size_t i = 0; i < prog.exprs.size(); ++i) {
    const bool last = (i + 1 == prog.exprs.size());
    print_expr(prog.arena, prog.exprs[i], out, "", last);
  }
  return out;
}

} // namespace tree