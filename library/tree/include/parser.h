#pragma once

#include "ast.h"
#include <vector>

namespace tree {
struct Token;
class DiagnosticEngine;

Program parse(std::vector<Token> tokens, DiagnosticEngine &diag);
} // namespace tree
