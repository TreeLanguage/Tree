#pragma once

#include "token.hpp"
#include <string_view>
#include <vector>

namespace tree {
class DiagnosticEngine;

std::vector<Token> lexer(std::string_view source, DiagnosticEngine &diag);
} // namespace tree