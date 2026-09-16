#pragma once
#include "ast.h"
#include <string>

namespace dsl {
// Renders the AST as an S-expression-like string. Deterministic; used for
// --emit=ast and as golden output in parser tests.
std::string ast_to_string(const Program& p);
std::string expr_to_string(const Expr& e);
} // namespace dsl
