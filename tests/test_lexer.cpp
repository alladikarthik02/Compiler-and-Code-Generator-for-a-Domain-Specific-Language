#include "test.h"
#include "lexer.h"
using namespace dsl;

static std::vector<Token> lex(const std::string& s) {
    Lexer l(s);
    return l.tokenize();
}

static void test_basic_tokens() {
    auto t = lex("fn main");
    CHECK_EQ((int)t[0].kind, (int)Tok::KwFn);
    CHECK_EQ((int)t[1].kind, (int)Tok::Ident);
    CHECK_EQ(t[1].lexeme, std::string("main"));
    CHECK_EQ((int)t[2].kind, (int)Tok::Eof);
}

static void test_multichar_operators() {
    // The classic maximal-munch traps: -> vs -, == vs =, <= vs <, != vs !.
    auto t = lex("-> - == = <= < >= > != ! && ||");
    Tok expect[] = {Tok::Arrow, Tok::Minus, Tok::Eq, Tok::Assign, Tok::Le, Tok::Lt,
                    Tok::Ge, Tok::Gt, Tok::Ne, Tok::Bang, Tok::AndAnd, Tok::OrOr, Tok::Eof};
    for (size_t i = 0; i < sizeof(expect)/sizeof(expect[0]); ++i)
        CHECK_EQ((int)t[i].kind, (int)expect[i]);
}

static void test_integers() {
    auto t = lex("0 42 1000");
    CHECK_EQ((int)t[0].kind, (int)Tok::Int);
    CHECK_EQ(t[0].int_val, (int64_t)0);
    CHECK_EQ(t[1].int_val, (int64_t)42);
    CHECK_EQ(t[2].int_val, (int64_t)1000);
}

static void test_keywords_vs_idents() {
    auto t = lex("let letx int integer if iffy");
    CHECK_EQ((int)t[0].kind, (int)Tok::KwLet);
    CHECK_EQ((int)t[1].kind, (int)Tok::Ident);   // "letx" is not a keyword
    CHECK_EQ((int)t[2].kind, (int)Tok::KwInt);
    CHECK_EQ((int)t[3].kind, (int)Tok::Ident);   // "integer"
    CHECK_EQ((int)t[4].kind, (int)Tok::KwIf);
    CHECK_EQ((int)t[5].kind, (int)Tok::Ident);   // "iffy"
}

static void test_comments_and_positions() {
    auto t = lex("let // this is ignored\nx");
    CHECK_EQ((int)t[0].kind, (int)Tok::KwLet);
    CHECK_EQ((int)t[1].kind, (int)Tok::Ident);
    CHECK_EQ(t[1].lexeme, std::string("x"));
    CHECK_EQ(t[1].line, 2);   // comment consumed the rest of line 1
    CHECK_EQ(t[1].col, 1);
}

static void test_lex_errors() {
    Lexer l("let x = 3 @ 4");
    auto t = l.tokenize();
    CHECK(!l.ok());
    CHECK_EQ((int)l.errors().size(), 1);
    (void)t;
}

int main() {
    test_basic_tokens();
    test_multichar_operators();
    test_integers();
    test_keywords_vs_idents();
    test_comments_and_positions();
    test_lex_errors();
    TEST_SUMMARY("lexer");
}
