#include "diagnostic.hpp"
#include <algorithm>
#include <compare>
#include <ostream>
#include <utility>

namespace tree {

std::string_view to_string(Severity severity) noexcept {
  switch (severity) {
  case Severity::Error:
    return "error";
  case Severity::Warning:
    return "warning";
  }
  return "unknown";
}

DiagnosticEngine::DiagnosticEngine(std::string filename, std::string source)
    : filename_(std::move(filename)), source_(std::move(source)) {
  std::string_view remaining = source_;
  while (true) {
    size_t newline = remaining.find('\n');
    if (newline == std::string_view::npos) {
      lines_.push_back(remaining);
      break;
    }
    lines_.push_back(remaining.substr(0, newline));
    remaining = remaining.substr(newline + 1);
  }
}

Diagnostic &DiagnosticEngine::report(Severity severity, Span span,
                                     std::string message) {
  diagnostics_.push_back(Diagnostic{severity, span, std::move(message)});
  return diagnostics_.back();
}

bool DiagnosticEngine::has_errors() const noexcept {
  return count(Severity::Error) > 0;
}

size_t DiagnosticEngine::count(Severity severity) const noexcept {
  return static_cast<size_t>(std::count_if(
      diagnostics_.begin(), diagnostics_.end(),
      [severity](const Diagnostic &d) { return d.severity == severity; }));
}

std::string_view DiagnosticEngine::line_text(int line_number) const {
  size_t index = static_cast<size_t>(line_number - 1);
  if (index >= lines_.size()) {
    return {};
  }
  return lines_[index];
}

void DiagnosticEngine::print_location(std::ostream &os, Span span) const {
  os << "  --> " << filename_ << ':' << span.begin << '\n';

  std::string_view text = line_text(span.begin.line);
  std::string line_number_str = std::to_string(span.begin.line);

  os << std::string(line_number_str.size(), ' ') << " |\n";
  os << line_number_str << " | " << text << '\n';
  os << std::string(line_number_str.size(), ' ') << " | ";

  int column = 1;
  for (; column < span.begin.column; ++column) {
    os << ' ';
  }
  int underline_end = (span.end.line == span.begin.line)
                          ? span.end.column
                          : static_cast<int>(text.size()) + 1;
  for (; column < underline_end; ++column) {
    os << '^';
  }
  os << '\n';
}

void DiagnosticEngine::print_one(std::ostream &os,
                                 const Diagnostic &diag) const {
  os << to_string(diag.severity) << ": " << diag.message << '\n';
  print_location(os, diag.span);

  os << '\n';
}

void DiagnosticEngine::print_all(std::ostream &os) const {
  std::vector<const Diagnostic *> sorted;
  sorted.reserve(diagnostics_.size());
  std::transform(diagnostics_.begin(), diagnostics_.end(),
                 std::back_inserter(sorted),
                 [](const Diagnostic &d) { return &d; });
  std::sort(sorted.begin(), sorted.end(),
            [](const Diagnostic *a, const Diagnostic *b) {
              return a->span.begin < b->span.begin;
            });

  for (const Diagnostic *d : sorted) {
    print_one(os, *d);
  }
}

} // namespace tree