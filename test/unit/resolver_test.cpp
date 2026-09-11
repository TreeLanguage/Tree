#include "ast.hpp"
#include "diagnostic.hpp"
#include "framework.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "resolver.hpp"
#include "token.hpp"
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using tree::BindingKind;
using tree::DiagnosticEngine;
using tree::Resolution;
using tree::Severity;

namespace {

struct ResolveResult {
  tree::Program prog;
  DiagnosticEngine diag;
  Resolution res;
};

ResolveResult resolve_src(const std::string &src) {
  DiagnosticEngine diag("<test>", src);
  std::vector<tree::Token> tokens = tree::lexer(src, diag);
  tree::Program prog = tree::parse(std::move(tokens), diag);
  Resolution res = tree::resolve(prog, diag);
  return ResolveResult{
      .prog = std::move(prog), .diag = std::move(diag), .res = std::move(res)};
}

std::vector<tree::ExprId> find_identifiers(const tree::Program &prog,
                                           const std::string &name) {
  std::vector<tree::ExprId> found;

  std::function<void(tree::ExprId)> walk = [&](tree::ExprId id) {
    const tree::Expr &e = prog.arena.get(id);
    tree::match(
        e.value,
        [&](const tree::Identifier &ident) {
          if (ident.name == name)
            found.push_back(id);
        },
        [&](const tree::TupleExpr &t) {
          for (const auto &f : t.fields)
            walk(f.value);
        },
        [&](const tree::Call &c) {
          walk(c.callee);
          walk(c.arg);
        },
        [&](const tree::BinaryExpr &b) {
          walk(b.lhs);
          walk(b.rhs);
        },
        [&](const tree::Lambda &l) { walk(l.body); },
        [&](const tree::MultiClauseLambda &l) {
          for (const auto &clause : l.clauses)
            walk(clause.body);
        },
        [&](const tree::Binding &b) { walk(b.value); }, [&](const auto &) {});
  };

  for (const tree::ExprId id : prog.exprs)
    walk(id);
  return found;
}

std::optional<tree::ExprId> find_identifier(const tree::Program &prog,
                                            const std::string &name) {
  auto all = find_identifiers(prog, name);
  if (all.empty())
    return std::nullopt;
  return all.front();
}

std::optional<tree::BindingId> top_level_binding(const Resolution &res,
                                                 const std::string &name) {
  for (uint32_t i = 0; i < res.bindings.size(); ++i) {
    const tree::BindingInfo &info = res.bindings[i];
    if ((info.kind == BindingKind::TopLevelFn ||
         info.kind == BindingKind::TopLevelValue) &&
        info.name == name) {
      return tree::BindingId{i};
    }
  }
  return std::nullopt;
}

} // namespace

TEST(resolves_simple_top_level_value) {
  auto r = resolve_src("x = 5\ny = x");
  CHECK(!r.diag.has_errors());
  auto x_id = find_identifier(r.prog, "x");
  CHECK(x_id.has_value());
  if (!x_id)
    return;
  auto it = r.res.identifier_binding.find(x_id->index);
  CHECK(it != r.res.identifier_binding.end());
  if (it == r.res.identifier_binding.end())
    return;
  const tree::BindingInfo &info = r.res.get(it->second);
  CHECK(info.kind == BindingKind::TopLevelValue);
  CHECK_EQ(info.name, std::string("x"));
}

TEST(resolves_lambda_parameter) {
  auto r = resolve_src("\\x -> x + 1");
  CHECK(!r.diag.has_errors());
  auto x_id = find_identifier(r.prog, "x");
  CHECK(x_id.has_value());
  if (!x_id)
    return;
  auto it = r.res.identifier_binding.find(x_id->index);
  CHECK(it != r.res.identifier_binding.end());
  if (it == r.res.identifier_binding.end())
    return;
  CHECK(r.res.get(it->second).kind == BindingKind::Local);
}

TEST(undefined_identifier_reports_error) {
  auto r = resolve_src("x = doesNotExist");
  CHECK(r.diag.has_errors());
  CHECK_EQ(r.diag.count(Severity::Error), static_cast<size_t>(1));
}

TEST(self_recursive_function_resolves) {
  auto r = resolve_src("factorial(n) = n * factorial((n - 1))");
  CHECK(!r.diag.has_errors());
  auto call_id = find_identifier(r.prog, "factorial");
  CHECK(call_id.has_value());
  if (!call_id)
    return;
  auto it = r.res.identifier_binding.find(call_id->index);
  CHECK(it != r.res.identifier_binding.end());
  if (it == r.res.identifier_binding.end())
    return;
  CHECK(r.res.get(it->second).kind == BindingKind::TopLevelFn);
  CHECK_EQ(r.res.get(it->second).name, std::string("factorial"));
}

TEST(mutually_recursive_functions_resolve_regardless_of_order) {
  auto r = resolve_src("filterList((_, ())) = ()\n"
                       "filterList((pred, (h, t))) = select((pred(h), h, "
                       "filterList((pred, t))))\n"
                       "\n"
                       "select((false, h, rest)) = (h, rest)\n"
                       "select((true, h, rest)) = rest\n");
  CHECK(!r.diag.has_errors());
  auto select_call = find_identifier(r.prog, "select");
  CHECK(select_call.has_value());
  if (!select_call)
    return;
  CHECK(r.res.identifier_binding.contains(select_call->index));
}

TEST(mutual_recursion_lands_in_same_binding_group) {
  auto r = resolve_src("isEven(0) = true\n"
                       "isEven(n) = isOdd((n - 1))\n"
                       "\n"
                       "isOdd(0) = false\n"
                       "isOdd(n) = isEven((n - 1))\n");
  CHECK(!r.diag.has_errors());
  auto even_id = top_level_binding(r.res, "isEven");
  auto odd_id = top_level_binding(r.res, "isOdd");
  CHECK(even_id.has_value());
  CHECK(odd_id.has_value());
  if (!even_id || !odd_id)
    return;
  bool same_group = false;
  for (const auto &group : r.res.binding_groups) {
    bool has_even = false;
    bool has_odd = false;
    for (const tree::BindingId id : group) {
      if (id == *even_id)
        has_even = true;
      if (id == *odd_id)
        has_odd = true;
    }
    if (has_even && has_odd) {
      same_group = true;
      break;
    }
  }
  CHECK(same_group);
}

TEST(one_directional_dependency_lands_in_separate_groups) {
  auto r = resolve_src("filterList((_, ())) = ()\n"
                       "filterList((pred, (h, t))) = select((pred(h), h, "
                       "filterList((pred, t))))\n"
                       "\n"
                       "select((false, h, rest)) = (h, rest)\n"
                       "select((true, h, rest)) = rest\n");
  CHECK(!r.diag.has_errors());
  auto filter_id = top_level_binding(r.res, "filterList");
  auto select_id = top_level_binding(r.res, "select");
  CHECK(filter_id.has_value());
  CHECK(select_id.has_value());
  if (!filter_id || !select_id)
    return;
  const bool same_group = false;
  size_t select_group_idx = SIZE_MAX;
  size_t filter_group_idx = SIZE_MAX;
  for (size_t i = 0; i < r.res.binding_groups.size(); ++i) {
    for (const tree::BindingId id : r.res.binding_groups[i]) {
      if (id == *filter_id)
        filter_group_idx = i;
      if (id == *select_id)
        select_group_idx = i;
    }
  }
  CHECK(filter_group_idx != SIZE_MAX);
  CHECK(select_group_idx != SIZE_MAX);
  CHECK(filter_group_idx != select_group_idx);
  CHECK(select_group_idx < filter_group_idx);
}

TEST(non_recursive_functions_land_in_separate_groups) {
  auto r = resolve_src("add((x, y)) = x + y\nsquare(x) = x * x");
  CHECK(!r.diag.has_errors());
  auto add_id = top_level_binding(r.res, "add");
  auto square_id = top_level_binding(r.res, "square");
  CHECK(add_id.has_value());
  CHECK(square_id.has_value());
  if (!add_id || !square_id)
    return;
  bool same_group = false;
  for (const auto &group : r.res.binding_groups) {
    bool has_add = false;
    bool has_square = false;
    for (const tree::BindingId id : group) {
      if (id == *add_id)
        has_add = true;
      if (id == *square_id)
        has_square = true;
    }
    if (has_add && has_square)
      same_group = true;
  }
  CHECK(!same_group);
}

TEST(non_adjacent_duplicate_function_reports_error) {
  auto r = resolve_src("f(x) = x\ng(y) = y\nf(x) = x + 1");
  CHECK(r.diag.has_errors());
  CHECK(r.diag.count(Severity::Error) >= static_cast<size_t>(1));
}

TEST(duplicate_binder_in_single_pattern_reports_error) {
  auto r = resolve_src("(x, x) = pair");
  CHECK(r.diag.has_errors());
  CHECK_EQ(r.diag.count(Severity::Error), static_cast<size_t>(1));
}

TEST(sequential_top_level_value_shadowing_is_not_an_error) {
  auto r = resolve_src("person = (name: \"Alice\", age: 30)\n"
                       "(name: n, age: a, _) = person\n"
                       "other = (name: \"Bob\", age: 40)\n"
                       "(name: n, age: a, _) = other\n"
                       "n\n");
  CHECK(!r.diag.has_errors());
}

TEST(later_shadowing_value_wins_for_subsequent_uses) {
  auto r = resolve_src("x = 1\nx = 2\ny = x");
  CHECK(!r.diag.has_errors());
  auto y_use = find_identifier(r.prog, "x");
  CHECK(y_use.has_value());
  if (!y_use)
    return;
  auto it = r.res.identifier_binding.find(y_use->index);
  CHECK(it != r.res.identifier_binding.end());
  if (it == r.res.identifier_binding.end())
    return;
  std::vector<tree::BindingId> x_bindings;
  for (uint32_t i = 0; i < r.res.bindings.size(); ++i) {
    if (r.res.bindings[i].name == "x" &&
        r.res.bindings[i].kind == BindingKind::TopLevelValue) {
      x_bindings.push_back(tree::BindingId{i});
    }
  }
  CHECK_EQ(x_bindings.size(), static_cast<size_t>(2));
  if (x_bindings.size() != 2)
    return;
  CHECK(it->second == x_bindings[1]);
}

TEST(nested_tuple_destructure_binds_all_locals) {
  auto r = resolve_src("fibAcc((n, (a, b))) = a + b + n");
  CHECK(!r.diag.has_errors());
  for (const char *name : {"n", "a", "b"}) {
    auto id = find_identifier(r.prog, name);
    CHECK(id.has_value());
    if (!id)
      continue;
    auto it = r.res.identifier_binding.find(id->index);
    CHECK(it != r.res.identifier_binding.end());
    if (it != r.res.identifier_binding.end()) {
      CHECK(r.res.get(it->second).kind == BindingKind::Local);
    }
  }
}

TEST(wildcard_and_literal_patterns_bind_nothing) {
  auto r = resolve_src("first((_, x)) = x\nfactorial(0) = 1");
  CHECK(!r.diag.has_errors());
}

TEST(named_field_pattern_binder_resolves_in_body) {
  auto r = resolve_src("getX((x: px, _)) = px");
  CHECK(!r.diag.has_errors());
  auto px_id = find_identifier(r.prog, "px");
  CHECK(px_id.has_value());
  if (!px_id)
    return;
  auto it = r.res.identifier_binding.find(px_id->index);
  CHECK(it != r.res.identifier_binding.end());
  if (it != r.res.identifier_binding.end()) {
    CHECK(r.res.get(it->second).kind == BindingKind::Local);
  }
}

TEST(function_body_can_reference_later_defined_function) {
  auto r = resolve_src("caller(x) = helper(x)\nhelper(x) = x + 1");
  CHECK(!r.diag.has_errors());
}

TEST(local_shadows_top_level_name) {
  auto r = resolve_src("x = 1\nuseLocal(x) = x + 1");
  CHECK(!r.diag.has_errors());
  bool found_local = false;
  for (uint32_t i = 0; i < r.prog.arena.expr_count(); ++i) {
    const tree::ExprId id{i};
    const tree::Expr &e = r.prog.arena.get(id);
    const auto *ident = std::get_if<tree::Identifier>(&e.value);
    if (ident == nullptr || ident->name != "x")
      continue;
    auto it = r.res.identifier_binding.find(id.index);
    if (it == r.res.identifier_binding.end())
      continue;
    if (r.res.get(it->second).kind == BindingKind::Local) {
      found_local = true;
    }
  }
  CHECK(found_local);
}