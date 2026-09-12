#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "span.h"

namespace tree {

enum class Severity : uint8_t {
    Error,
    Warning
};

std::string_view to_string(Severity severity) noexcept;

struct Diagnostic {
    Severity severity{};
    Span span;
    std::string message;
};

class DiagnosticEngine {
public:
    DiagnosticEngine(std::string filename, std::string source);

    Diagnostic& report(Severity severity, Span span, std::string message);

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] size_t count(Severity severity) const noexcept;

    [[nodiscard]]
    const std::vector<Diagnostic>& all() const noexcept {
        return diagnostics_;
    }

    void print_all(std::ostream& os) const;

private:
    [[nodiscard]] std::string_view line_text(int line_number) const;

    void print_one(std::ostream& os, const Diagnostic& diag) const;
    void print_location(std::ostream& os, Span span) const;

    std::string filename_;
    std::string source_;
    std::vector<std::string_view> lines_;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace tree