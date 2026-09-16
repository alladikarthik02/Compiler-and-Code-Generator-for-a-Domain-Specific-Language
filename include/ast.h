#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace dsl {

// The only value types in the language. Error is the "poison" type Sema uses to
// avoid cascading messages after a type error.
enum class Type { Int, Bool, Error };

const char* type_name(Type t);

enum class BinOp { Add, Sub, Mul, Div, Mod, Lt, Le, Gt, Ge, Eq, Ne, And, Or };
enum class UnOp  { Neg, Not };

const char* binop_str(BinOp op);
const char* unop_str(UnOp op);

// ---- Expressions -------------------------------------------------------
enum class ExprKind { IntLit, BoolLit, Var, Unary, Binary, Call };

struct Expr {
    ExprKind kind;
    Type type = Type::Error;   // filled in by Sema
    int line = 0, col = 0;
    explicit Expr(ExprKind k) : kind(k) {}
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLit : Expr {
    int64_t value;
    explicit IntLit(int64_t v) : Expr(ExprKind::IntLit), value(v) {}
};
struct BoolLit : Expr {
    bool value;
    explicit BoolLit(bool v) : Expr(ExprKind::BoolLit), value(v) {}
};
struct VarExpr : Expr {
    std::string name;
    explicit VarExpr(std::string n) : Expr(ExprKind::Var), name(std::move(n)) {}
};
struct UnaryExpr : Expr {
    UnOp op;
    ExprPtr operand;
    UnaryExpr(UnOp o, ExprPtr e) : Expr(ExprKind::Unary), op(o), operand(std::move(e)) {}
};
struct BinaryExpr : Expr {
    BinOp op;
    ExprPtr lhs, rhs;
    BinaryExpr(BinOp o, ExprPtr l, ExprPtr r)
        : Expr(ExprKind::Binary), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
};
struct CallExpr : Expr {
    std::string callee;
    std::vector<ExprPtr> args;
    explicit CallExpr(std::string c) : Expr(ExprKind::Call), callee(std::move(c)) {}
};

// ---- Statements --------------------------------------------------------
enum class StmtKind { Let, Assign, If, While, Return, Print, ExprStmt, Block };

struct Stmt {
    StmtKind kind;
    int line = 0, col = 0;
    explicit Stmt(StmtKind k) : kind(k) {}
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct Block : Stmt {
    std::vector<StmtPtr> stmts;
    Block() : Stmt(StmtKind::Block) {}
};
using BlockPtr = std::unique_ptr<Block>;

struct LetStmt : Stmt {
    std::string name;
    Type declared;
    ExprPtr init;
    LetStmt() : Stmt(StmtKind::Let) {}
};
struct AssignStmt : Stmt {
    std::string name;
    ExprPtr value;
    AssignStmt() : Stmt(StmtKind::Assign) {}
};
struct IfStmt : Stmt {
    ExprPtr cond;
    BlockPtr then_blk;
    BlockPtr else_blk;   // may be null
    IfStmt() : Stmt(StmtKind::If) {}
};
struct WhileStmt : Stmt {
    ExprPtr cond;
    BlockPtr body;
    WhileStmt() : Stmt(StmtKind::While) {}
};
struct ReturnStmt : Stmt {
    ExprPtr value;       // always present in this language
    ReturnStmt() : Stmt(StmtKind::Return) {}
};
struct PrintStmt : Stmt {
    ExprPtr value;
    PrintStmt() : Stmt(StmtKind::Print) {}
};
struct ExprStmt : Stmt {
    ExprPtr expr;
    ExprStmt() : Stmt(StmtKind::ExprStmt) {}
};

// ---- Functions & program ----------------------------------------------
struct Param { std::string name; Type type; };

struct Function {
    std::string name;
    std::vector<Param> params;
    Type ret_type;
    BlockPtr body;
    int line = 0, col = 0;
};

struct Program {
    std::vector<std::unique_ptr<Function>> functions;
};

} // namespace dsl
