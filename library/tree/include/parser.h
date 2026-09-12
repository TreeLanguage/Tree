#pragma once

#include <vector>

#include "ast.h"

namespace tree {

struct Token;
class DiagnosticEngine;

Program parse(std::vector<Token> tokens, DiagnosticEngine& diag);

}  // namespace tree