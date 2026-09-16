#pragma once
#include "ir.h"
#include "bytecode.h"

namespace dsl {
// Lowers optimized IR into executable register bytecode.
BCProgram codegen(const IRModule& m);
} // namespace dsl
