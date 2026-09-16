#pragma once
#include "bytecode.h"
#include <ostream>
#include <string>
#include <cstdint>

namespace dsl {

struct VMResult {
    bool ok = true;
    int64_t value = 0;        // return value of main
    std::string error;        // set when ok == false (e.g. division by zero)
};

// Executes the program starting at main(). BCProgram output (print) goes to `out`.
VMResult run_program(const BCProgram& p, std::ostream& out);

} // namespace dsl
