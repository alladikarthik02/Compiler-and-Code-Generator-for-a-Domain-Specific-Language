#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dsl {

// Register-based bytecode. Registers map 1:1 to IR temps; named locals live in
// a separate `slots` array. Jump targets are absolute instruction indices
// (resolved from basic-block ids at lowering time).

enum class BOp {
    LoadConst,   // reg[dst] = imm
    Move,        // reg[dst] = val(a)
    Add, Sub, Mul, Div, Mod,
    Lt, Le, Gt, Ge, Eq, Ne, And, Or,
    Neg, Not,    // reg[dst] = op val(a)
    LoadSlot,    // reg[dst] = slot[slot_idx]
    StoreSlot,   // slot[slot_idx] = val(a)
    Print,       // print val(a)
    Jmp,         // pc = target
    JmpIfFalse,  // if val(a) == 0: pc = target
    Call,        // reg[dst] = funcs[callee](val(args)...)
    Ret,         // return val(a)
};

// An operand is either an immediate constant or a register reference.
struct Operand {
    bool imm = false;
    int reg = -1;
    int64_t val = 0;
    static Operand r(int reg_id) { Operand o; o.imm = false; o.reg = reg_id; return o; }
    static Operand c(int64_t v)  { Operand o; o.imm = true;  o.val = v; return o; }
};

struct BC {
    BOp op;
    int dst = -1;
    Operand a, b;
    int slot_idx = -1;
    int target = -1;             // for Jmp/JmpIfFalse (absolute pc)
    int callee = -1;             // function index for Call
    std::vector<Operand> args;   // for Call
};

struct BCFunction {
    std::string name;
    int num_regs = 0;
    int num_slots = 0;
    int num_params = 0;
    std::vector<BC> code;
};

struct BCProgram {
    std::vector<BCFunction> funcs;
    int main_index = -1;
};

std::string bc_to_string(const BCProgram& p);

} // namespace dsl
