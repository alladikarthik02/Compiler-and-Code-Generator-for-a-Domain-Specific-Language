#include "ast_print.h"
#include <sstream>

namespace dsl {

std::string expr_to_string(const Expr& e) {
    std::ostringstream os;
    switch (e.kind) {
        case ExprKind::IntLit:
            os << static_cast<const IntLit&>(e).value; break;
        case ExprKind::BoolLit:
            os << (static_cast<const BoolLit&>(e).value ? "true" : "false"); break;
        case ExprKind::Var:
            os << static_cast<const VarExpr&>(e).name; break;
        case ExprKind::Unary: {
            auto& u = static_cast<const UnaryExpr&>(e);
            os << "(" << unop_str(u.op) << " " << expr_to_string(*u.operand) << ")";
            break;
        }
        case ExprKind::Binary: {
            auto& b = static_cast<const BinaryExpr&>(e);
            os << "(" << binop_str(b.op) << " " << expr_to_string(*b.lhs)
               << " " << expr_to_string(*b.rhs) << ")";
            break;
        }
        case ExprKind::Call: {
            auto& c = static_cast<const CallExpr&>(e);
            os << "(call " << c.callee;
            for (auto& a : c.args) os << " " << expr_to_string(*a);
            os << ")";
            break;
        }
    }
    return os.str();
}

static void indent(std::ostream& os, int n) { for (int i = 0; i < n; ++i) os << "  "; }

static void print_stmt(std::ostream& os, const Stmt& s, int depth);

static void print_block(std::ostream& os, const Block& b, int depth) {
    for (auto& s : b.stmts) print_stmt(os, *s, depth);
}

static void print_stmt(std::ostream& os, const Stmt& s, int depth) {
    indent(os, depth);
    switch (s.kind) {
        case StmtKind::Let: {
            auto& l = static_cast<const LetStmt&>(s);
            os << "let " << l.name << ":" << type_name(l.declared)
               << " = " << expr_to_string(*l.init) << "\n";
            break;
        }
        case StmtKind::Assign: {
            auto& a = static_cast<const AssignStmt&>(s);
            os << "assign " << a.name << " = " << expr_to_string(*a.value) << "\n";
            break;
        }
        case StmtKind::If: {
            auto& i = static_cast<const IfStmt&>(s);
            os << "if " << expr_to_string(*i.cond) << "\n";
            print_block(os, *i.then_blk, depth + 1);
            if (i.else_blk) { indent(os, depth); os << "else\n"; print_block(os, *i.else_blk, depth + 1); }
            break;
        }
        case StmtKind::While: {
            auto& w = static_cast<const WhileStmt&>(s);
            os << "while " << expr_to_string(*w.cond) << "\n";
            print_block(os, *w.body, depth + 1);
            break;
        }
        case StmtKind::Return: {
            auto& r = static_cast<const ReturnStmt&>(s);
            os << "return " << expr_to_string(*r.value) << "\n";
            break;
        }
        case StmtKind::Print: {
            auto& p = static_cast<const PrintStmt&>(s);
            os << "print " << expr_to_string(*p.value) << "\n";
            break;
        }
        case StmtKind::ExprStmt: {
            auto& e = static_cast<const ExprStmt&>(s);
            os << "expr " << expr_to_string(*e.expr) << "\n";
            break;
        }
        case StmtKind::Block: {
            os << "block\n";
            print_block(os, static_cast<const Block&>(s), depth + 1);
            break;
        }
    }
}

std::string ast_to_string(const Program& p) {
    std::ostringstream os;
    for (auto& fn : p.functions) {
        os << "fn " << fn->name << "(";
        for (size_t i = 0; i < fn->params.size(); ++i) {
            if (i) os << ", ";
            os << fn->params[i].name << ":" << type_name(fn->params[i].type);
        }
        os << ") -> " << type_name(fn->ret_type) << "\n";
        print_block(os, *fn->body, 1);
    }
    return os.str();
}

} // namespace dsl
