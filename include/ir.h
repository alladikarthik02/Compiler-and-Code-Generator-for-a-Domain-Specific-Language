#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace dsl {

// ---------------------------------------------------------------------------
// Linear three-address IR:  Module -> Function -> BasicBlock -> Instruction.
//
//  * A "value" is either a virtual register (temp, assigned exactly once -> SSA
//    for temps) or an inline integer/bool constant.
//  * Named variables are NOT SSA; they live in numbered slots reached via
//    Load/Store. This keeps optimization on temps trivially sound and avoids
//    phi-node insertion (see docs/SPEC.md sec.6).
//  * Every BasicBlock ends in exactly one terminator (Br / CondBr / Ret).
// ---------------------------------------------------------------------------

enum class Op {
    // pure, value-producing
    Const,       // dst = imm
    Copy,        // dst = a   (introduced by algebraic simplification: x+0 -> x)
    Add, Sub, Mul, Div, Mod,
    Lt, Le, Gt, Ge, Eq, Ne,
    And, Or,     // NOTE: only emitted for already-evaluated bools; short-circuit
                 // control flow is lowered to branches, not these ops.
    Neg, Not,
    Load,        // dst = load slot
    Call,        // dst = call fn(args...)
    // side-effecting / control (no dst, or dst ignored)
    Store,       // store slot, a
    Print,       // print a
    Br,          // goto bb_true
    CondBr,      // if a goto bb_true else bb_false
    Ret,         // return a
};

const char* op_str(Op op);
bool op_is_terminator(Op op);
bool op_has_side_effect(Op op);   // Store/Print/Call/terminators

// A value operand: either a temp id or an immediate constant.
struct Value {
    enum Kind { Temp, ConstInt } kind = Temp;
    int temp = -1;          // valid when kind == Temp
    int64_t imm = 0;        // valid when kind == ConstInt

    static Value temp_val(int id) { Value v; v.kind = Temp; v.temp = id; return v; }
    static Value const_val(int64_t c) { Value v; v.kind = ConstInt; v.imm = c; return v; }
    bool is_const() const { return kind == ConstInt; }
    bool is_temp() const { return kind == Temp; }
    bool operator==(const Value& o) const {
        return kind == o.kind && (kind == Temp ? temp == o.temp : imm == o.imm);
    }
};

struct Instr {
    Op op;
    int dst = -1;                  // result temp id, or -1 if none
    std::vector<Value> args;       // operand values
    int slot = -1;                 // for Load/Store: which local slot
    std::string callee;            // for Call
    int bb_true = -1, bb_false = -1; // for Br / CondBr
    bool dead = false;             // marked by DCE, swept at end of a pass
};

struct BasicBlock {
    int id = -1;
    std::string label;             // e.g. "entry", "then.3"
    std::vector<Instr> instrs;

    const Instr* terminator() const {
        if (instrs.empty()) return nullptr;
        const Instr& last = instrs.back();
        return op_is_terminator(last.op) ? &last : nullptr;
    }
};

struct IRFunction {
    std::string name;
    int num_params = 0;            // params occupy slots [0, num_params)
    int num_slots = 0;             // total local slots (params + locals)
    int next_temp = 0;             // temp id allocator
    std::vector<BasicBlock> blocks;

    int new_temp() { return next_temp++; }
    int new_slot() { return num_slots++; }
    BasicBlock& block(int id) { return blocks[id]; }
};

struct IRModule {
    std::vector<IRFunction> funcs;
};

// Pretty-printer used for --emit=ir, golden tests, and eyeballing.
std::string ir_to_string(const IRModule& m);
std::string ir_to_string(const IRFunction& f);

// Count of live (non-dead) instructions across the module -- the metric the
// optimization tests use to prove passes shrink the code.
int ir_instr_count(const IRModule& m);
int ir_instr_count(const IRFunction& f);

} // namespace dsl
