#pragma once
#include <string>
#include <cstdint>

namespace dsl {

enum class Tok {
    // literals & identifiers
    Int, Ident,
    // keywords
    KwFn, KwLet, KwIf, KwElse, KwWhile, KwReturn, KwPrint, KwInt, KwBool, KwTrue, KwFalse,
    // punctuation
    LParen, RParen, LBrace, RBrace, Comma, Colon, Semicolon, Arrow,
    // operators
    Plus, Minus, Star, Slash, Percent,
    Assign, Eq, Ne, Lt, Le, Gt, Ge,
    AndAnd, OrOr, Bang,
    // control
    Eof, Error,
};

const char* tok_name(Tok t);

struct Token {
    Tok kind;
    std::string lexeme; // exact source text
    int line;           // 1-based
    int col;            // 1-based, column of first char
    int64_t int_val = 0; // valid when kind == Int

    bool operator==(const Token& o) const {
        return kind == o.kind && lexeme == o.lexeme && line == o.line && col == o.col;
    }
};

} // namespace dsl
