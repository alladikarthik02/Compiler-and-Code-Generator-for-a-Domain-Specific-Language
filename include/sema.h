#pragma once
#include "ast.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace dsl {

struct SemaError {
    std::string message;
    int line, col;
};

struct FuncSig {
    std::vector<Type> params;
    Type ret;
};

// Type-checks a program in place: annotates every Expr with its resolved type
// and reports diagnostics. Also exposes the function signature table, which
// IRGen needs to lower calls.
class Sema {
public:
    // Walks the program, filling Expr::type and collecting errors.
    bool check(Program& prog);

    const std::vector<SemaError>& errors() const { return errors_; }
    const std::unordered_map<std::string, FuncSig>& functions() const { return funcs_; }

private:
    // scope stack of name -> type
    void push_scope() { scopes_.emplace_back(); }
    void pop_scope()  { scopes_.pop_back(); }
    bool declare(const std::string& name, Type t);       // false if redeclared in current scope
    bool lookup(const std::string& name, Type& out) const;

    void error(int line, int col, const std::string& msg);

    void check_function(Function& fn);
    void check_block(Block& blk);
    void check_stmt(Stmt& s);
    Type check_expr(Expr& e);          // returns resolved type, annotates e.type

    static bool always_returns(const Block& blk);
    static bool stmt_always_returns(const Stmt& s);

    std::unordered_map<std::string, FuncSig> funcs_;
    std::vector<std::unordered_map<std::string, Type>> scopes_;
    Type cur_ret_ = Type::Error;       // return type of the function being checked
    std::vector<SemaError> errors_;
};

} // namespace dsl
