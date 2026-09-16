#pragma once
#include "ast.h"
#include "token.h"
#include <string>
#include <vector>

namespace dsl {

struct ParseError {
    std::string message;
    int line, col;
};

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string filename = "<input>");

    // Parses a whole program. On error, collects diagnostics (see errors()) and
    // recovers to keep going where possible. Returns whatever AST it built.
    std::unique_ptr<Program> parse_program();

    const std::vector<ParseError>& errors() const { return errors_; }
    bool ok() const { return errors_.empty(); }

private:
    // token cursor
    const Token& peek(int ahead = 0) const;
    const Token& cur() const { return peek(0); }
    bool check(Tok k) const { return cur().kind == k; }
    bool at_end() const { return cur().kind == Tok::Eof; }
    const Token& advance();
    bool match(Tok k);
    const Token& expect(Tok k, const char* what);

    void error(const std::string& msg);
    void error_at(const Token& t, const std::string& msg);
    void synchronize();       // skip to a statement/decl boundary after an error

    // grammar
    std::unique_ptr<Function> parse_function();
    Type parse_type();
    BlockPtr parse_block();
    StmtPtr parse_statement();
    StmtPtr parse_let();
    StmtPtr parse_if();
    StmtPtr parse_while();
    StmtPtr parse_return();
    StmtPtr parse_print();
    StmtPtr parse_simple_stmt();  // assignment or expr-stmt

    // Pratt expression parser
    ExprPtr parse_expr();
    ExprPtr parse_binary(int min_prec);
    ExprPtr parse_unary();
    ExprPtr parse_primary();

    std::vector<Token> toks_;
    std::string file_;
    size_t pos_ = 0;
    std::vector<ParseError> errors_;
    bool panicking_ = false;   // suppress cascade errors until we resync
};

} // namespace dsl
