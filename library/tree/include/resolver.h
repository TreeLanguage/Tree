#pragma once

#include "ast.h"
#include "diagnostic.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tree {

struct BindingId {
  uint32_t index = UINT32_MAX;
  [[nodiscard]] bool valid() const { return index != UINT32_MAX; }
  friend bool operator==(BindingId, BindingId) = default;
};

enum class BindingKind : uint8_t {
  TopLevelValue,
  TopLevelFn,
  Local,
};

struct BindingInfo {
  BindingKind kind{};
  std::string name;
  Span span;
  ExprId top_level_expr;
};

struct Resolution {
  std::unordered_map<uint32_t, BindingId> identifier_binding;
  std::unordered_map<uint32_t, BindingId> pattern_binding;
  std::vector<BindingInfo> bindings;
  std::unordered_map<uint32_t, std::vector<BindingId>> top_level_deps;
  std::vector<std::vector<BindingId>> binding_groups;

  [[nodiscard]] const BindingInfo &get(BindingId id) const {
    return bindings[id.index];
  }
};

Resolution resolve(const Program &prog, DiagnosticEngine &diag);

} // namespace tree
