#include "sema.h"
#include <sstream>

namespace dsl {

void Sema::error(int line, int col, const std::string& msg) {
    errors_.push_back({msg, line, col});
}

bool Sema::declare(const std::string& name, Type t) {
    auto& top = scopes_.back();
    if (top.count(name)) return false;
    top[name] = t;
    return true;
}

bool Sema::lookup(const std::string& name, Type& out) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) { out = f->second; return true; }
    }
    return false;
}

bool Sema::check(Program& prog) {
    // Pass 1: collect signatures so calls can be forward-referenced.
    for (auto& fn : prog.functions) {
        if (funcs_.count(fn->name)) {
            error(fn->line, fn->col, "duplicate function '" + fn->name + "'");
            continue;
        }
        FuncSig sig; sig.ret = fn->ret_type;
        for (auto& p : fn->params) sig.params.push_back(p.type);
        funcs_[fn->name] = sig;
    }

    // The VM entry point must be main() -> int with no params.
    auto mit = funcs_.find("main");
    if (mit == funcs_.end()) {
        error(0, 0, "program has no 'main' function");
    } else if (!mit->second.params.empty() || mit->second.ret != Type::Int) {
        error(0, 0, "'main' must have signature 'fn main() -> int'");
    }

    // Pass 2: check bodies.
    for (auto& fn : prog.functions) check_function(*fn);
    return errors_.empty();
}

void Sema::check_function(Function& fn) {
    cur_ret_ = fn.ret_type;
    push_scope();
    for (auto& p : fn.params) {
        if (!declare(p.name, p.type))
            error(fn.line, fn.col, "duplicate parameter '" + p.name + "' in '" + fn.name + "'");
    }
    check_block(*fn.body);
    pop_scope();

    if (!always_returns(*fn.body))
        error(fn.line, fn.col, "not all control paths in '" + fn.name + "' return a value");
}

void Sema::check_block(Block& blk) {
    push_scope();
    for (auto& s : blk.stmts) check_stmt(*s);
    pop_scope();
}

void Sema::check_stmt(Stmt& s) {
    switch (s.kind) {
        case StmtKind::Let: {
            auto& l = static_cast<LetStmt&>(s);
            Type init = check_expr(*l.init);
            if (init != Type::Error && init != l.declared)
                error(s.line, s.col, "cannot initialize '" + l.name + "' of type " +
                      type_name(l.declared) + " with value of type " + type_name(init));
            if (!declare(l.name, l.declared))
                error(s.line, s.col, "redeclaration of '" + l.name + "' in this scope");
            break;
        }
        case StmtKind::Assign: {
            auto& a = static_cast<AssignStmt&>(s);
            Type vt = check_expr(*a.value);
            Type dt;
            if (!lookup(a.name, dt)) {
                error(s.line, s.col, "assignment to undeclared variable '" + a.name + "'");
            } else if (vt != Type::Error && vt != dt) {
                error(s.line, s.col, "cannot assign value of type " + std::string(type_name(vt)) +
                      " to '" + a.name + "' of type " + type_name(dt));
            }
            break;
        }
        case StmtKind::If: {
            auto& i = static_cast<IfStmt&>(s);
            Type c = check_expr(*i.cond);
            if (c != Type::Error && c != Type::Bool)
                error(i.cond->line, i.cond->col, "if condition must be bool, got " + std::string(type_name(c)));
            check_block(*i.then_blk);
            if (i.else_blk) check_block(*i.else_blk);
            break;
        }
        case StmtKind::While: {
            auto& w = static_cast<WhileStmt&>(s);
            Type c = check_expr(*w.cond);
            if (c != Type::Error && c != Type::Bool)
                error(w.cond->line, w.cond->col, "while condition must be bool, got " + std::string(type_name(c)));
            check_block(*w.body);
            break;
        }
        case StmtKind::Return: {
            auto& r = static_cast<ReturnStmt&>(s);
            Type vt = check_expr(*r.value);
            if (vt != Type::Error && vt != cur_ret_)
                error(s.line, s.col, "return type mismatch: function returns " +
                      std::string(type_name(cur_ret_)) + " but value is " + type_name(vt));
            break;
        }
        case StmtKind::Print: {
            auto& p = static_cast<PrintStmt&>(s);
            Type vt = check_expr(*p.value);
            if (vt != Type::Error && vt != Type::Int)
                error(s.line, s.col, "print expects an int, got " + std::string(type_name(vt)));
            break;
        }
        case StmtKind::ExprStmt: {
            auto& e = static_cast<ExprStmt&>(s);
            check_expr(*e.expr);
            break;
        }
        case StmtKind::Block:
            check_block(static_cast<Block&>(s));
            break;
    }
}

Type Sema::check_expr(Expr& e) {
    switch (e.kind) {
        case ExprKind::IntLit:  return e.type = Type::Int;
        case ExprKind::BoolLit: return e.type = Type::Bool;
        case ExprKind::Var: {
            auto& v = static_cast<VarExpr&>(e);
            Type t;
            if (!lookup(v.name, t)) {
                error(e.line, e.col, "use of undeclared variable '" + v.name + "'");
                return e.type = Type::Error;
            }
            return e.type = t;
        }
        case ExprKind::Unary: {
            auto& u = static_cast<UnaryExpr&>(e);
            Type ot = check_expr(*u.operand);
            if (ot == Type::Error) return e.type = Type::Error;
            if (u.op == UnOp::Neg) {
                if (ot != Type::Int) { error(e.line, e.col, "unary '-' expects int"); return e.type = Type::Error; }
                return e.type = Type::Int;
            } else { // Not
                if (ot != Type::Bool) { error(e.line, e.col, "unary '!' expects bool"); return e.type = Type::Error; }
                return e.type = Type::Bool;
            }
        }
        case ExprKind::Binary: {
            auto& b = static_cast<BinaryExpr&>(e);
            Type lt = check_expr(*b.lhs);
            Type rt = check_expr(*b.rhs);
            if (lt == Type::Error || rt == Type::Error) return e.type = Type::Error;
            switch (b.op) {
                case BinOp::Add: case BinOp::Sub: case BinOp::Mul:
                case BinOp::Div: case BinOp::Mod:
                    if (lt != Type::Int || rt != Type::Int) {
                        error(e.line, e.col, std::string("operator '") + binop_str(b.op) + "' expects int operands");
                        return e.type = Type::Error;
                    }
                    return e.type = Type::Int;
                case BinOp::Lt: case BinOp::Le: case BinOp::Gt: case BinOp::Ge:
                    if (lt != Type::Int || rt != Type::Int) {
                        error(e.line, e.col, std::string("operator '") + binop_str(b.op) + "' expects int operands");
                        return e.type = Type::Error;
                    }
                    return e.type = Type::Bool;
                case BinOp::Eq: case BinOp::Ne:
                    if (lt != rt) {
                        error(e.line, e.col, std::string("operator '") + binop_str(b.op) +
                              "' expects operands of the same type");
                        return e.type = Type::Error;
                    }
                    return e.type = Type::Bool;
                case BinOp::And: case BinOp::Or:
                    if (lt != Type::Bool || rt != Type::Bool) {
                        error(e.line, e.col, std::string("operator '") + binop_str(b.op) + "' expects bool operands");
                        return e.type = Type::Error;
                    }
                    return e.type = Type::Bool;
            }
            return e.type = Type::Error;
        }
        case ExprKind::Call: {
            auto& c = static_cast<CallExpr&>(e);
            auto it = funcs_.find(c.callee);
            // Evaluate args regardless, so their subexpressions get typed / errors reported.
            std::vector<Type> arg_types;
            for (auto& a : c.args) arg_types.push_back(check_expr(*a));
            if (it == funcs_.end()) {
                error(e.line, e.col, "call to undefined function '" + c.callee + "'");
                return e.type = Type::Error;
            }
            const FuncSig& sig = it->second;
            if (sig.params.size() != arg_types.size()) {
                error(e.line, e.col, "function '" + c.callee + "' expects " +
                      std::to_string(sig.params.size()) + " argument(s) but got " +
                      std::to_string(arg_types.size()));
                return e.type = sig.ret;   // still know the result type
            }
            for (size_t i = 0; i < arg_types.size(); ++i) {
                if (arg_types[i] != Type::Error && arg_types[i] != sig.params[i])
                    error(c.args[i]->line, c.args[i]->col, "argument " + std::to_string(i + 1) +
                          " of '" + c.callee + "' expects " + type_name(sig.params[i]) +
                          " but got " + type_name(arg_types[i]));
            }
            return e.type = sig.ret;
        }
    }
    return e.type = Type::Error;
}

// ---- "all paths return" analysis --------------------------------------
bool Sema::stmt_always_returns(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Return: return true;
        case StmtKind::If: {
            auto& i = static_cast<const IfStmt&>(s);
            if (!i.else_blk) return false;    // no else => may fall through
            return always_returns(*i.then_blk) && always_returns(*i.else_blk);
        }
        case StmtKind::Block:
            return always_returns(static_cast<const Block&>(s));
        default:
            return false;   // while is NOT guaranteed to run/return
    }
}

bool Sema::always_returns(const Block& blk) {
    for (auto& s : blk.stmts)
        if (stmt_always_returns(*s)) return true;
    return false;
}

} // namespace dsl
