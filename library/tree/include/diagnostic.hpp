#pragma once

#include "span.hpp"
#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace tree {

enum class Severity { Error, Warning };

std::string_view to_string(Severity severity) noexcept;

struct Diagnostic {
  Severity severity = {};
  Span span;
  std::string message;
};

class DiagnosticEngine {
public:
  DiagnosticEngine(std::string filename, std::string source);

  Diagnostic &report(Severity severity, Span span, std::string message);

  bool has_errors() const noexcept;
  size_t count(Severity severity) const noexcept;

  void print_all(std::ostream &os) const;

private:
  std::string_view line_text(int line_number) const;

  void print_one(std::ostream &os, const Diagnostic &diag) const;
  void print_location(std::ostream &os, Span span) const;

  std::string filename_;
  std::string source_;
  std::vector<std::string_view> lines_;
  std::vector<Diagnostic> diagnostics_;
};

} // namespace tree