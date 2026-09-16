#include "test.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
using namespace dsl;

// Returns number of sema errors (or -1 if it didn't even parse).
static int sema_errors(const std::string& src) {
    Lexer l(src);
    Parser p(l.tokenize());
    auto prog = p.parse_program();
    if (!p.ok()) return -1;
    Sema s;
    s.check(*prog);
    return (int)s.errors().size();
}

// Wrap an expression/statement body inside main for convenience.
static std::string in_main(const std::string& body) {
    return "fn main() -> int {\n" + body + "\n  return 0;\n}\n";
}

static void test_valid_programs() {
    CHECK_EQ(sema_errors(in_main("  let x: int = 1 + 2;")), 0);
    CHECK_EQ(sema_errors(in_main("  let b: bool = 1 < 2;")), 0);
    CHECK_EQ(sema_errors(in_main("  let b: bool = true && false;")), 0);
    CHECK_EQ(sema_errors(
        "fn add(a: int, b: int) -> int { return a + b; }\n" + in_main("  print(add(1, 2));")), 0);
}

static void test_type_errors() {
    // int + bool
    CHECK(sema_errors(in_main("  let x: int = 1 + true;")) > 0);
    // declared int, init bool
    CHECK(sema_errors(in_main("  let x: int = 1 < 2;")) > 0);
    // if condition must be bool
    CHECK(sema_errors(in_main("  if (5) { print(1); }")) > 0);
    // unary ! on int
    CHECK(sema_errors(in_main("  let b: bool = !3;")) > 0);
    // print a bool
    CHECK(sema_errors(in_main("  print(true);")) > 0);
    // == across types
    CHECK(sema_errors(in_main("  let b: bool = 1 == true;")) > 0);
}

static void test_scope_and_decl() {
    // use before declaration
    CHECK(sema_errors(in_main("  x = 3;")) > 0);
    // undeclared read
    CHECK(sema_errors(in_main("  let y: int = z + 1;")) > 0);
    // redeclaration in same scope
    CHECK(sema_errors(in_main("  let x: int = 1; let x: int = 2;")) > 0);
    // shadowing in inner scope is allowed
    CHECK_EQ(sema_errors(in_main("  let x: int = 1; if (true) { let x: int = 2; print(x); }")), 0);
    // variable does not escape its block
    CHECK(sema_errors(in_main("  if (true) { let inner: int = 1; } print(inner);")) > 0);
}

static void test_calls() {
    std::string add = "fn add(a: int, b: int) -> int { return a + b; }\n";
    CHECK_EQ(sema_errors(add + in_main("  print(add(1, 2));")), 0);
    CHECK(sema_errors(add + in_main("  print(add(1));")) > 0);        // arity
    CHECK(sema_errors(add + in_main("  print(add(1, true));")) > 0);  // arg type
    CHECK(sema_errors(in_main("  print(nope(1));")) > 0);             // undefined fn
}

static void test_return_paths() {
    // missing return on a path
    CHECK(sema_errors("fn f() -> int { if (true) { return 1; } }\n" + in_main("  return 0;")) > 0);
    // both branches return -> ok
    CHECK_EQ(sema_errors("fn f() -> int { if (true) { return 1; } else { return 2; } }\n"
                         + in_main("  print(f());")), 0);
    // while alone does not guarantee return
    CHECK(sema_errors("fn f() -> int { while (true) { return 1; } }\n" + in_main("  return 0;")) > 0);
}

static void test_main_signature() {
    CHECK(sema_errors("fn notmain() -> int { return 0; }") > 0);     // no main
    CHECK(sema_errors("fn main() -> bool { return true; }") > 0);    // wrong ret
    CHECK(sema_errors("fn main(x: int) -> int { return x; }") > 0);  // has params
}

int main() {
    test_valid_programs();
    test_type_errors();
    test_scope_and_decl();
    test_calls();
    test_return_paths();
    test_main_signature();
    TEST_SUMMARY("sema");
}
