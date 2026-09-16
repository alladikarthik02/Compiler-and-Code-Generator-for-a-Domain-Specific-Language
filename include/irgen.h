#pragma once
#include "ast.h"
#include "ir.h"
#include <unordered_map>
#include <vector>
#include <string>

namespace dsl {

// Lowers a type-checked AST into the linear IR. Assumes Sema has already
// accepted the program (types are trusted here).
class IRGen {
public:
    IRModule generate(const Program& prog);

private:
    IRFunction gen_function(const Function& fn);

    // statement / block lowering
    void gen_block(const Block& b);
    void gen_stmt(const Stmt& s);

    // expression lowering -> produces an operand Value (temp or inline const)
    Value gen_expr(const Expr& e);
    Value gen_short_circuit(const BinaryExpr& b);  // && / ||

    // block / emit helpers
    int new_block(const std::string& label);
    void set_cur(int bb) { cur_ = bb; terminated_ = false; }
    Instr& emit(Instr in);
    void emit_terminator(Instr in);
    void finalize_terminators();     // give every block a terminator

    // slot scope management
    void push_scope() { scopes_.emplace_back(); }
    void pop_scope()  { scopes_.pop_back(); }
    int  declare_slot(const std::string& name);
    int  lookup_slot(const std::string& name) const;

    IRFunction* f_ = nullptr;
    int cur_ = -1;              // current block id
    bool terminated_ = false;   // has cur_ already got its terminator?
    std::vector<std::unordered_map<std::string, int>> scopes_;
    int label_ctr_ = 0;
};

} // namespace dsl
