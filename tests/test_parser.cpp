#include "test.h"
#include "lexer.h"
#include "parser.h"
#include "ast_print.h"
using namespace dsl;

// Parse a single expression by wrapping it and pulling it back out.
static std::string parse_expr_str(const std::string& expr) {
    std::string src = "fn f() -> int { return " + expr + "; }";
    Lexer l(src);
    Parser p(l.tokenize());
    auto prog = p.parse_program();
    if (!p.ok() || prog->functions.empty()) return "<parse-error>";
    auto& body = prog->functions[0]->body->stmts;
    if (body.empty()) return "<empty>";
    auto* ret = static_cast<ReturnStmt*>(body[0].get());
    return expr_to_string(*ret->value);
}

static void test_precedence() {
    // * binds tighter than +
    CHECK_EQ(parse_expr_str("1 + 2 * 3"), std::string("(+ 1 (* 2 3))"));
    // left associativity of -
    CHECK_EQ(parse_expr_str("10 - 2 - 3"), std::string("(- (- 10 2) 3)"));
    // comparison below arithmetic
    CHECK_EQ(parse_expr_str("1 + 2 < 3 * 4"), std::string("(< (+ 1 2) (* 3 4))"));
    // && below ==, || below &&
    CHECK_EQ(parse_expr_str("a == 1 && b == 2 || c"),
             std::string("(|| (&& (== a 1) (== b 2)) c)"));
    // unary minus binds tighter than *
    CHECK_EQ(parse_expr_str("-a * b"), std::string("(* (- a) b)"));
    // parentheses override
    CHECK_EQ(parse_expr_str("(1 + 2) * 3"), std::string("(* (+ 1 2) 3)"));
}

static void test_calls() {
    CHECK_EQ(parse_expr_str("gcd(a, b + 1)"), std::string("(call gcd a (+ b 1))"));
    CHECK_EQ(parse_expr_str("f()"), std::string("(call f)"));
    CHECK_EQ(parse_expr_str("f(g(x))"), std::string("(call f (call g x))"));
}

static void test_full_program() {
    const char* src =
        "fn add(a: int, b: int) -> int {\n"
        "  return a + b;\n"
        "}\n"
        "fn main() -> int {\n"
        "  let x: int = 3;\n"
        "  if (x < 5) { print(x); } else { print(0); }\n"
        "  while (x > 0) { x = x - 1; }\n"
        "  return add(x, 1);\n"
        "}\n";
    Lexer l(src);
    Parser p(l.tokenize());
    auto prog = p.parse_program();
    CHECK(p.ok());
    CHECK_EQ((int)prog->functions.size(), 2);
    CHECK_EQ(prog->functions[0]->name, std::string("add"));
    CHECK_EQ((int)prog->functions[0]->params.size(), 2);
    CHECK_EQ(prog->functions[1]->name, std::string("main"));
}

static void test_error_recovery() {
    // Two separate errors should both be reported thanks to synchronize().
    const char* src =
        "fn main() -> int {\n"
        "  let x: int = ;\n"      // error 1: missing expr
        "  let y: int = 2 2;\n"   // error 2: junk after expr (missing ';')
        "  return x;\n"
        "}\n";
    Lexer l(src);
    Parser p(l.tokenize());
    p.parse_program();
    CHECK(!p.ok());
    CHECK(p.errors().size() >= 2);
}

int main() {
    test_precedence();
    test_calls();
    test_full_program();
    test_error_recovery();
    TEST_SUMMARY("parser");
}
