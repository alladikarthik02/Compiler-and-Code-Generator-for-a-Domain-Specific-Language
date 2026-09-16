#include "test.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "irgen.h"
#include "pass.h"
#include "codegen.h"
#include "vm.h"
#include <sstream>
using namespace dsl;

struct RunOut { bool compiled; bool ran_ok; std::string out; std::string err; int64_t value; };

static RunOut run(const std::string& src, bool optimize) {
    RunOut ro{};
    Lexer l(src);
    Parser p(l.tokenize());
    auto prog = p.parse_program();
    Sema s;
    ro.compiled = p.ok() && s.check(*prog);
    if (!ro.compiled) return ro;
    IRGen g;
    IRModule m = g.generate(*prog);
    if (optimize) { auto pm = default_pipeline(); pm->run(m); }
    BCProgram bc = codegen(m);
    std::ostringstream os;
    VMResult r = run_program(bc, os);
    ro.ran_ok = r.ok; ro.out = os.str(); ro.err = r.error; ro.value = r.value;
    return ro;
}

// -- concrete programs with known output --------------------------------
static const char* FIB =
    "fn fib(n: int) -> int {\n"
    "  if (n < 2) { return n; }\n"
    "  return fib(n - 1) + fib(n - 2);\n"
    "}\n"
    "fn main() -> int { print(fib(10)); return 0; }\n";

static const char* FACT =
    "fn fact(n: int) -> int {\n"
    "  let acc: int = 1;\n"
    "  while (n > 1) { acc = acc * n; n = n - 1; }\n"
    "  return acc;\n"
    "}\n"
    "fn main() -> int { print(fact(5)); return 0; }\n";

static const char* GCD =
    "fn gcd(a: int, b: int) -> int { while (b != 0) { let t: int = b; b = a % b; a = t; } return a; }\n"
    "fn main() -> int { print(gcd(48, 36)); print(gcd(17, 5)); return 0; }\n";

static const char* SUMLOOP =
    "fn main() -> int { let i: int = 1; let s: int = 0; while (i <= 100) { s = s + i; i = i + 1; } print(s); return 0; }\n";

static const char* NESTED =
    "fn main() -> int {\n"
    "  let n: int = 3;\n"
    "  let out: int = 0;\n"
    "  let i: int = 0;\n"
    "  while (i < n) {\n"
    "    let j: int = 0;\n"
    "    while (j < n) { out = out + i * n + j; j = j + 1; }\n"
    "    i = i + 1;\n"
    "  }\n"
    "  print(out);\n"
    "  return 0;\n"
    "}\n";

static void test_known_outputs() {
    CHECK_EQ(run(FIB, true).out, std::string("55\n"));
    CHECK_EQ(run(FACT, true).out, std::string("120\n"));
    CHECK_EQ(run(GCD, true).out, std::string("12\n1\n"));
    CHECK_EQ(run(SUMLOOP, true).out, std::string("5050\n"));
}

// THE key correctness property: optimization must never change observable
// behavior. Run each program at -O0 and -O1 and demand identical output.
static void test_semantic_preservation() {
    const char* corpus[] = { FIB, FACT, GCD, SUMLOOP, NESTED };
    for (auto* src : corpus) {
        RunOut o0 = run(src, false);
        RunOut o1 = run(src, true);
        CHECK(o0.compiled && o1.compiled);
        CHECK_EQ(o0.ran_ok, o1.ran_ok);
        CHECK_EQ(o0.out, o1.out);
        CHECK_EQ(o0.value, o1.value);
    }
}

// Short-circuit && must not evaluate the RHS when the LHS is false -- here that
// avoids a division-by-zero trap. Must hold at BOTH -O0 and -O1.
static void test_short_circuit_avoids_trap() {
    const char* src =
        "fn main() -> int {\n"
        "  let x: int = 0;\n"
        "  if (x != 0 && (10 / x) > 1) { print(1); } else { print(2); }\n"
        "  return 0;\n"
        "}\n";
    RunOut o0 = run(src, false), o1 = run(src, true);
    CHECK(o0.ran_ok);  CHECK_EQ(o0.out, std::string("2\n"));
    CHECK(o1.ran_ok);  CHECK_EQ(o1.out, std::string("2\n"));
}

static void test_runtime_trap() {
    const char* src = "fn main() -> int { let z: int = 0; print(10 / z); return 0; }";
    RunOut o = run(src, true);
    CHECK(!o.ran_ok);
    CHECK(o.err.find("division by zero") != std::string::npos);
}

static void test_or_short_circuit() {
    // true || (10/0) must short-circuit and print 1.
    const char* src =
        "fn main() -> int { let x: int = 0; if (x == 0 || (10 / x) > 0) { print(1); } else { print(9); } return 0; }";
    RunOut o = run(src, true);
    CHECK(o.ran_ok);
    CHECK_EQ(o.out, std::string("1\n"));
}

// Integer overflow must WRAP (two's complement) rather than invoke undefined
// behaviour, and -- critically -- constant folding at compile time must produce
// exactly what the VM computes at runtime. If those two ever disagree, -O0 and
// -O1 diverge and the whole correctness guarantee breaks.
// Regression test: UBSan found signed-overflow UB in fold_binary and in the VM's
// arithmetic. See docs/CHALLENGES.md CHAL-008.
static void test_overflow_wraps_and_is_consistent() {
    struct { const char* expr; const char* expect; } cases[] = {
        {"9223372036854775807 + 1",  "-9223372036854775808\n"},  // INT64_MAX + 1
        {"-9223372036854775807 - 2",  "9223372036854775807\n"},  // INT64_MIN - 1
        {"4611686018427387904 * 4",   "0\n"},                    // 2^62 * 4
    };
    for (auto& c : cases) {
        std::string src = std::string("fn main() -> int { print(") + c.expr + "); return 0; }";
        RunOut o1 = run(src, true);    // folded at compile time
        RunOut o0 = run(src, false);   // computed at runtime by the VM
        CHECK(o1.ran_ok && o0.ran_ok);
        CHECK_EQ(o1.out, std::string(c.expect));
        CHECK_EQ(o0.out, o1.out);      // folder and VM must agree exactly
    }
    // INT64_MIN / -1 and INT64_MIN % -1 overflow too; they must not trap or crash.
    {
        const char* src = "fn main() -> int { let m: int = -9223372036854775807 - 1; "
                          "let d: int = 0 - 1; print(m / d); print(m % d); return 0; }";
        RunOut o1 = run(src, true), o0 = run(src, false);
        CHECK(o1.ran_ok && o0.ran_ok);
        CHECK_EQ(o1.out, std::string("-9223372036854775808\n0\n"));
        CHECK_EQ(o0.out, o1.out);
    }
}

int main() {
    test_overflow_wraps_and_is_consistent();
    test_known_outputs();
    test_semantic_preservation();
    test_short_circuit_avoids_trap();
    test_runtime_trap();
    test_or_short_circuit();
    TEST_SUMMARY("e2e");
}
