#include "ast.hpp"
#include "diagnostic.hpp"
#include "resolver.hpp"
#include "span.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {
struct Scope {
  std::unordered_map<std::string, tree::BindingId> locals;
  Scope *parent = nullptr;

  [[nodiscard]] std::optional<tree::BindingId>
  lookup(const std::string &name) const {
    for (const Scope *s = this; s != nullptr; s = s->parent) {
      auto it = s->locals.find(name);
      if (it != s->locals.end()) {
        return it->second;
      }
    }
    return std::nullopt;
  }
};

class Resolver {
public:
  Resolver(const tree::Program &prog, tree::DiagnosticEngine &diag)
      : prog_(prog), arena_(prog.arena), diag_(diag) {}

  tree::Resolution run() {
    collect_top_level_functions();
    resolve_sequential();
    compute_binding_groups();
    return std::move(res_);
  }

private:
  const tree::Program &prog_;
  const tree::Arena &arena_;
  tree::DiagnosticEngine &diag_;
  tree::Resolution res_;
  std::unordered_map<std::string, tree::BindingId> top_level_;
  std::optional<tree::BindingId> current_top_level_;

  tree::BindingId add_binding(tree::BindingKind kind, std::string name,
                              tree::Span span,
                              tree::ExprId top_level_expr = {}) {
    tree::BindingId id{static_cast<uint32_t>(res_.bindings.size())};
    res_.bindings.push_back(
        tree::BindingInfo{.kind = kind,
                          .name = std::move(name),
                          .span = span,
                          .top_level_expr = top_level_expr});
    return id;
  }

  void collect_top_level_functions() {
    for (tree::ExprId id : prog_.exprs) {
      const tree::Expr &e = arena_.get(id);
      const auto *binding = std::get_if<tree::Binding>(&e.value);
      if (binding == nullptr)
        continue;

      const tree::Expr &value_expr = arena_.get(binding->value);
      tree::match(
          value_expr.value,
          [&](const tree::Lambda &l) {
            if (l.name)
              register_top_level_name(*l.name, e.span, id);
          },
          [&](const tree::MultiClauseLambda &l) {
            if (l.name)
              register_top_level_name(*l.name, e.span, id);
          },
          [&](const auto &) {});
    }
  }

  void register_top_level_name(const std::string &name, tree::Span span,
                               tree::ExprId owner) {
    if (top_level_.contains(name)) {
      diag_.report(tree::Severity::Error, span,
                   "duplicate top-level function '" + name + "'");
      return;
    }
    tree::BindingId id =
        add_binding(tree::BindingKind::TopLevelFn, name, span, owner);
    top_level_.emplace(name, id);
  }

  void register_top_level_pattern(tree::PatternId pid, tree::ExprId owner) {
    const tree::Pattern &p = arena_.get(pid);
    tree::match(
        p.value,
        [&](const tree::VarPattern &vp) {
          if (top_level_.contains(vp.name)) {
            diag_.report(tree::Severity::Error, p.span,
                         "duplicate top-level name '" + vp.name + "'");
            return;
          }
          tree::BindingId id = add_binding(tree::BindingKind::TopLevelValue,
                                           vp.name, p.span, owner);
          top_level_.emplace(vp.name, id);
          res_.pattern_binding[pid.index] = id;
        },
        [&](const tree::TuplePattern &tp) {
          for (const auto &field : tp.fields) {
            register_top_level_pattern(field.pattern, owner);
          }
        },
        [&](const auto &) {});
  }

  void resolve_lambda_like(const tree::Lambda &l, Scope &parent) {
    Scope fn_scope{.locals = {}, .parent = &parent};
    bind_pattern(l.param, fn_scope);
    resolve_expr(l.body, fn_scope);
  }

  void resolve_multi_clause(const tree::MultiClauseLambda &l, Scope &parent) {
    for (const auto &clause : l.clauses) {
      Scope clause_scope{.locals = {}, .parent = &parent};
      bind_pattern(clause.param, clause_scope);
      resolve_expr(clause.body, clause_scope);
    }
  }

  void bind_pattern(tree::PatternId pid, Scope &scope,
                    std::unordered_set<std::string> *seen = nullptr) {
    std::unordered_set<std::string> local_seen;
    if (seen == nullptr)
      seen = &local_seen;

    const tree::Pattern &p = arena_.get(pid);
    tree::match(
        p.value,
        [&](const tree::VarPattern &vp) {
          if (!seen->insert(vp.name).second) {
            diag_.report(tree::Severity::Error, p.span,
                         "duplicate binder '" + vp.name + "' in pattern");
            return;
          }
          const tree::BindingId id =
              add_binding(tree::BindingKind::Local, vp.name, p.span);
          scope.locals[vp.name] = id;
          res_.pattern_binding[pid.index] = id;
        },
        [&](const tree::TuplePattern &tp) {
          for (const auto &field : tp.fields) {
            bind_pattern(field.pattern, scope, seen);
          }
        },
        [&](const auto &) {});
  }

  void bind_top_level_pattern(tree::PatternId pid, tree::ExprId owner) {
    const tree::Pattern &p = arena_.get(pid);
    tree::match(
        p.value,
        [&](const tree::VarPattern &vp) {
          const tree::BindingId id = add_binding(
              tree::BindingKind::TopLevelValue, vp.name, p.span, owner);
          top_level_[vp.name] = id;
          res_.pattern_binding[pid.index] = id;
        },
        [&](const tree::TuplePattern &tp) {
          for (const auto &field : tp.fields) {
            bind_top_level_pattern(field.pattern, owner);
          }
        },
        [&](const auto &) {});
  }

  void resolve_expr(tree::ExprId id, Scope &scope) {
    const tree::Expr &e = arena_.get(id);
    tree::match(
        e.value,
        [&](const tree::Identifier &ident) {
          resolve_identifier(id, ident, e.span, scope);
        },
        [&](const tree::TupleExpr &t) {
          for (const auto &f : t.fields)
            resolve_expr(f.value, scope);
        },
        [&](const tree::Call &c) {
          resolve_expr(c.callee, scope);
          resolve_expr(c.arg, scope);
        },
        [&](const tree::BinaryExpr &b) {
          resolve_expr(b.lhs, scope);
          resolve_expr(b.rhs, scope);
        },
        [&](const tree::Lambda &l) { resolve_lambda_like(l, scope); },
        [&](const tree::MultiClauseLambda &l) {
          resolve_multi_clause(l, scope);
        },
        [&](const tree::Binding &b) {
          resolve_expr(b.value, scope);
          bind_pattern(b.target, scope);
        },
        [&](const auto &) {});
  }

  void resolve_identifier(tree::ExprId id, const tree::Identifier &ident,
                          tree::Span span, Scope &scope) {
    if (auto local = scope.lookup(ident.name)) {
      res_.identifier_binding[id.index] = *local;
      return;
    }
    auto it = top_level_.find(ident.name);
    if (it == top_level_.end()) {
      diag_.report(tree::Severity::Error, span,
                   "undefined identifier '" + ident.name + "'");
      return;
    }
    res_.identifier_binding[id.index] = it->second;
    if (current_top_level_) {
      res_.top_level_deps[current_top_level_->index].push_back(it->second);
    }
  }

  void resolve_sequential() {
    for (tree::ExprId id : prog_.exprs) {
      const tree::Expr &e = arena_.get(id);
      Scope root;
      tree::match(
          e.value,
          [&](const tree::Binding &b) {
            const tree::Expr &value_expr = arena_.get(b.value);
            const bool is_fn =
                std::holds_alternative<tree::Lambda>(value_expr.value) ||
                std::holds_alternative<tree::MultiClauseLambda>(
                    value_expr.value);

            if (is_fn) {
              const auto *var =
                  std::get_if<tree::VarPattern>(&arena_.get(b.target).value);
              current_top_level_ =
                  var ? std::optional(top_level_.at(var->name)) : std::nullopt;
              resolve_expr(b.value, root);
            } else {
              current_top_level_ = std::nullopt;
              resolve_expr(b.value, root);
              bind_top_level_pattern(b.target, id);
            }
          },
          [&](const tree::Lambda &l) {
            current_top_level_ =
                l.name ? std::optional(top_level_.at(*l.name)) : std::nullopt;
            resolve_lambda_like(l, root);
          },
          [&](const tree::MultiClauseLambda &l) {
            current_top_level_ =
                l.name ? std::optional(top_level_.at(*l.name)) : std::nullopt;
            resolve_multi_clause(l, root);
          },
          [&](const auto &) {
            current_top_level_ = std::nullopt;
            resolve_expr(id, root);
          });
    }
  }

  void compute_binding_groups() {
    const size_t n = res_.bindings.size();
    std::vector<int> index(n, -1);
    std::vector<int> low(n, -1);
    std::vector<bool> on_stack(n, false);
    std::vector<uint32_t> stack;
    int counter = 0;

    std::function<void(uint32_t)> strongconnect = [&](uint32_t v) {
      index[v] = low[v] = counter++;
      stack.push_back(v);
      on_stack[v] = true;

      auto it = res_.top_level_deps.find(v);
      if (it != res_.top_level_deps.end()) {
        for (const tree::BindingId w : it->second) {
          if (index[w.index] == -1) {
            strongconnect(w.index);
            low[v] = std::min(low[v], low[w.index]);
          } else if (on_stack[w.index]) {
            low[v] = std::min(low[v], index[w.index]);
          }
        }
      }

      if (low[v] == index[v]) {
        std::vector<tree::BindingId> group;
        for (;;) {
          const uint32_t w = stack.back();
          stack.pop_back();
          on_stack[w] = false;
          group.push_back(tree::BindingId{w});
          if (w == v) {
            break;
          }
        }
        res_.binding_groups.push_back(std::move(group));
      }
    };

    for (uint32_t i = 0; i < n; ++i) {
      if (res_.bindings[i].kind == tree::BindingKind::Local)
        continue;
      if (index[i] == -1)
        strongconnect(i);
    }
    std::ranges::reverse(res_.binding_groups);
  }
};

} // namespace

namespace tree {
Resolution resolve(const Program &prog, DiagnosticEngine &diag) {
  Resolver r(prog, diag);
  return r.run();
}
} // namespace tree