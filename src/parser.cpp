#include "parser.h"
#include <cstdio>

namespace dsl {

const char* type_name(Type t) {
    switch (t) { case Type::Int: return "int"; case Type::Bool: return "bool";
                 case Type::Error: return "<error>"; } return "?";
}
const char* binop_str(BinOp op) {
    switch (op) {
        case BinOp::Add: return "+"; case BinOp::Sub: return "-"; case BinOp::Mul: return "*";
        case BinOp::Div: return "/"; case BinOp::Mod: return "%"; case BinOp::Lt: return "<";
        case BinOp::Le: return "<="; case BinOp::Gt: return ">"; case BinOp::Ge: return ">=";
        case BinOp::Eq: return "=="; case BinOp::Ne: return "!="; case BinOp::And: return "&&";
        case BinOp::Or: return "||";
    } return "?";
}
const char* unop_str(UnOp op) { return op == UnOp::Neg ? "-" : "!"; }

// Binary operator table: token -> (BinOp, left binding power). 0 == not binary.
struct BinInfo { BinOp op; int prec; bool is_binop; };
static BinInfo bin_info(Tok k) {
    switch (k) {
        case Tok::OrOr:    return {BinOp::Or,  1, true};
        case Tok::AndAnd:  return {BinOp::And, 2, true};
        case Tok::Eq:      return {BinOp::Eq,  3, true};
        case Tok::Ne:      return {BinOp::Ne,  3, true};
        case Tok::Lt:      return {BinOp::Lt,  4, true};
        case Tok::Le:      return {BinOp::Le,  4, true};
        case Tok::Gt:      return {BinOp::Gt,  4, true};
        case Tok::Ge:      return {BinOp::Ge,  4, true};
        case Tok::Plus:    return {BinOp::Add, 5, true};
        case Tok::Minus:   return {BinOp::Sub, 5, true};
        case Tok::Star:    return {BinOp::Mul, 6, true};
        case Tok::Slash:   return {BinOp::Div, 6, true};
        case Tok::Percent: return {BinOp::Mod, 6, true};
        default:           return {BinOp::Add, 0, false};
    }
}

Parser::Parser(std::vector<Token> tokens, std::string filename)
    : toks_(std::move(tokens)), file_(std::move(filename)) {}

const Token& Parser::peek(int ahead) const {
    size_t p = pos_ + ahead;
    if (p >= toks_.size()) return toks_.back(); // Eof
    return toks_[p];
}
const Token& Parser::advance() {
    if (!at_end()) pos_++;
    return toks_[pos_ - 1];
}
bool Parser::match(Tok k) { if (check(k)) { advance(); return true; } return false; }

const Token& Parser::expect(Tok k, const char* what) {
    if (check(k)) return advance();
    error(std::string("expected ") + what + " but found '" + cur().lexeme + "'");
    return cur();
}

void Parser::error(const std::string& msg) { error_at(cur(), msg); }
void Parser::error_at(const Token& t, const std::string& msg) {
    if (panicking_) return;      // suppress cascade until we resync
    panicking_ = true;
    errors_.push_back({msg, t.line, t.col});
}

// Recover after a syntax error: consume tokens until we're just past a ';' or
// at something that clearly starts a new statement/decl, then clear panic mode.
void Parser::synchronize() {
    panicking_ = false;
    while (!at_end()) {
        if (toks_[pos_ - 1].kind == Tok::Semicolon) return;
        switch (cur().kind) {
            case Tok::KwFn: case Tok::KwLet: case Tok::KwIf: case Tok::KwWhile:
            case Tok::KwReturn: case Tok::KwPrint: case Tok::RBrace:
                return;
            default: advance();
        }
    }
}

std::unique_ptr<Program> Parser::parse_program() {
    auto prog = std::make_unique<Program>();
    while (!at_end()) {
        if (check(Tok::KwFn)) {
            auto fn = parse_function();
            if (fn) prog->functions.push_back(std::move(fn));
            if (panicking_) synchronize();
        } else {
            error("expected 'fn' at top level");
            synchronize();
        }
    }
    return prog;
}

Type Parser::parse_type() {
    if (match(Tok::KwInt)) return Type::Int;
    if (match(Tok::KwBool)) return Type::Bool;
    error("expected type ('int' or 'bool')");
    return Type::Error;
}

std::unique_ptr<Function> Parser::parse_function() {
    const Token& fnTok = expect(Tok::KwFn, "'fn'");
    auto fn = std::make_unique<Function>();
    fn->line = fnTok.line; fn->col = fnTok.col;

    const Token& name = expect(Tok::Ident, "function name");
    fn->name = name.lexeme;

    expect(Tok::LParen, "'('");
    if (!check(Tok::RParen)) {
        do {
            const Token& p = expect(Tok::Ident, "parameter name");
            expect(Tok::Colon, "':'");
            Type pt = parse_type();
            fn->params.push_back({p.lexeme, pt});
        } while (match(Tok::Comma));
    }
    expect(Tok::RParen, "')'");
    expect(Tok::Arrow, "'->'");
    fn->ret_type = parse_type();
    fn->body = parse_block();
    return fn;
}

BlockPtr Parser::parse_block() {
    auto blk = std::make_unique<Block>();
    const Token& lb = expect(Tok::LBrace, "'{'");
    blk->line = lb.line; blk->col = lb.col;
    while (!check(Tok::RBrace) && !at_end()) {
        auto s = parse_statement();
        if (s) blk->stmts.push_back(std::move(s));
        if (panicking_) synchronize();
    }
    expect(Tok::RBrace, "'}'");
    return blk;
}

StmtPtr Parser::parse_statement() {
    switch (cur().kind) {
        case Tok::KwLet:    return parse_let();
        case Tok::KwIf:     return parse_if();
        case Tok::KwWhile:  return parse_while();
        case Tok::KwReturn: return parse_return();
        case Tok::KwPrint:  return parse_print();
        case Tok::LBrace:   return parse_block();
        default:            return parse_simple_stmt();
    }
}

StmtPtr Parser::parse_let() {
    auto s = std::make_unique<LetStmt>();
    const Token& kw = advance(); // let
    s->line = kw.line; s->col = kw.col;
    s->name = expect(Tok::Ident, "variable name").lexeme;
    expect(Tok::Colon, "':'");
    s->declared = parse_type();
    expect(Tok::Assign, "'='");
    s->init = parse_expr();
    expect(Tok::Semicolon, "';'");
    return s;
}

StmtPtr Parser::parse_if() {
    auto s = std::make_unique<IfStmt>();
    const Token& kw = advance(); // if
    s->line = kw.line; s->col = kw.col;
    expect(Tok::LParen, "'('");
    s->cond = parse_expr();
    expect(Tok::RParen, "')'");
    s->then_blk = parse_block();
    if (match(Tok::KwElse)) s->else_blk = parse_block();
    return s;
}

StmtPtr Parser::parse_while() {
    auto s = std::make_unique<WhileStmt>();
    const Token& kw = advance(); // while
    s->line = kw.line; s->col = kw.col;
    expect(Tok::LParen, "'('");
    s->cond = parse_expr();
    expect(Tok::RParen, "')'");
    s->body = parse_block();
    return s;
}

StmtPtr Parser::parse_return() {
    auto s = std::make_unique<ReturnStmt>();
    const Token& kw = advance(); // return
    s->line = kw.line; s->col = kw.col;
    s->value = parse_expr();
    expect(Tok::Semicolon, "';'");
    return s;
}

StmtPtr Parser::parse_print() {
    auto s = std::make_unique<PrintStmt>();
    const Token& kw = advance(); // print
    s->line = kw.line; s->col = kw.col;
    expect(Tok::LParen, "'('");
    s->value = parse_expr();
    expect(Tok::RParen, "')'");
    expect(Tok::Semicolon, "';'");
    return s;
}

// Disambiguate assignment ("IDENT = ...") from a bare expression statement.
StmtPtr Parser::parse_simple_stmt() {
    if (check(Tok::Ident) && peek(1).kind == Tok::Assign) {
        auto s = std::make_unique<AssignStmt>();
        const Token& id = advance();       // IDENT
        s->line = id.line; s->col = id.col;
        s->name = id.lexeme;
        advance();                          // '='
        s->value = parse_expr();
        expect(Tok::Semicolon, "';'");
        return s;
    }
    auto s = std::make_unique<ExprStmt>();
    s->line = cur().line; s->col = cur().col;
    s->expr = parse_expr();
    expect(Tok::Semicolon, "';'");
    return s;
}

// ---- Expressions (Pratt / precedence climbing) ------------------------
ExprPtr Parser::parse_expr() { return parse_binary(1); }

ExprPtr Parser::parse_binary(int min_prec) {
    ExprPtr lhs = parse_unary();
    for (;;) {
        BinInfo info = bin_info(cur().kind);
        if (!info.is_binop || info.prec < min_prec) break;
        const Token& opTok = advance();
        // Left-associative: right side must bind strictly tighter.
        ExprPtr rhs = parse_binary(info.prec + 1);
        auto b = std::make_unique<BinaryExpr>(info.op, std::move(lhs), std::move(rhs));
        b->line = opTok.line; b->col = opTok.col;
        lhs = std::move(b);
    }
    return lhs;
}

ExprPtr Parser::parse_unary() {
    if (check(Tok::Minus) || check(Tok::Bang)) {
        const Token& opTok = advance();
        UnOp op = (opTok.kind == Tok::Minus) ? UnOp::Neg : UnOp::Not;
        ExprPtr operand = parse_unary();  // right-associative
        auto u = std::make_unique<UnaryExpr>(op, std::move(operand));
        u->line = opTok.line; u->col = opTok.col;
        return u;
    }
    return parse_primary();
}

ExprPtr Parser::parse_primary() {
    const Token& t = cur();
    switch (t.kind) {
        case Tok::Int: {
            advance();
            auto e = std::make_unique<IntLit>(t.int_val);
            e->line = t.line; e->col = t.col; return e;
        }
        case Tok::KwTrue: case Tok::KwFalse: {
            advance();
            auto e = std::make_unique<BoolLit>(t.kind == Tok::KwTrue);
            e->line = t.line; e->col = t.col; return e;
        }
        case Tok::Ident: {
            advance();
            if (check(Tok::LParen)) {          // function call
                advance();
                auto call = std::make_unique<CallExpr>(t.lexeme);
                call->line = t.line; call->col = t.col;
                if (!check(Tok::RParen)) {
                    do { call->args.push_back(parse_expr()); } while (match(Tok::Comma));
                }
                expect(Tok::RParen, "')'");
                return call;
            }
            auto e = std::make_unique<VarExpr>(t.lexeme);
            e->line = t.line; e->col = t.col; return e;
        }
        case Tok::LParen: {
            advance();
            ExprPtr e = parse_expr();
            expect(Tok::RParen, "')'");
            return e;
        }
        default:
            error(std::string("expected expression but found '") + t.lexeme + "'");
            // Return a poison literal so callers don't segfault on null.
            auto e = std::make_unique<IntLit>(0);
            e->line = t.line; e->col = t.col;
            e->type = Type::Error;
            return e;
    }
}

} // namespace dsl
