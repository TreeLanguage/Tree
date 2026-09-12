#pragma once

#include <string_view>
#include <vector>

#include "token.h"

namespace tree {

class DiagnosticEngine;

std::vector<Token> lexer(std::string_view source, DiagnosticEngine& diag);

}  // namespace tree