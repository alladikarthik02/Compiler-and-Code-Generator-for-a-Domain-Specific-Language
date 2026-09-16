#include "test.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "irgen.h"
#include "ir.h"
#include "pass.h"
using namespace dsl;

static IRModule build(const std::string& src, bool optimize, bool& ok) {
    Lexer l(src);
    Parser p(l.tokenize());
    auto prog = p.parse_program();
    Sema s;
    ok = p.ok() && s.check(*prog);
    IRGen g;
    IRModule m = g.generate(*prog);
    if (optimize && ok) { auto pm = default_pipeline(); pm->run(m); }
    return m;
}

static const IRFunction* find_fn(const IRModule& m, const std::string& name) {
    for (auto& f : m.funcs) if (f.name == name) return &f;
    return nullptr;
}
static int count_op(const IRFunction& f, Op op) {
    int n = 0;
    for (auto& bb : f.blocks) for (auto& in : bb.instrs) if (in.op == op) n++;
    return n;
}
// The (single) Ret in a straight-line function.
static const Instr* find_ret(const IRFunction& f) {
    for (auto& bb : f.blocks) for (auto& in : bb.instrs) if (in.op == Op::Ret) return &in;
    return nullptr;
}

static void test_constant_folding() {
    bool ok; auto m = build("fn main() -> int { return 2 + 3 * 4; }", true, ok);
    CHECK(ok);
    auto* main = find_fn(m, "main");
    CHECK(main != nullptr);
    // All arithmetic folded away; function just returns the constant 14.
    CHECK_EQ(count_op(*main, Op::Add), 0);
    CHECK_EQ(count_op(*main, Op::Mul), 0);
    auto* r = find_ret(*main);
    CHECK(r && !r->args.empty() && r->args[0].is_const());
    CHECK_EQ(r->args[0].imm, (int64_t)14);
}

static void test_algebraic_times_zero() {
    bool ok; auto m = build("fn main() -> int { let x: int = 7; return x * 0; }", true, ok);
    CHECK(ok);
    auto* main = find_fn(m, "main");
    CHECK_EQ(count_op(*main, Op::Mul), 0);          // x*0 removed
    auto* r = find_ret(*main);
    CHECK(r && r->args[0].is_const() && r->args[0].imm == 0);
}

static void test_algebraic_times_one() {
    // (a*1) with a a runtime param must simplify to just a, no Mul left.
    bool ok; auto m = build(
        "fn f(a: int) -> int { return a * 1; }\nfn main() -> int { print(f(9)); return 0; }", true, ok);
    CHECK(ok);
    auto* f = find_fn(m, "f");
    CHECK_EQ(count_op(*f, Op::Mul), 0);
}

static void test_dead_code_removed() {
    const char* src =
        "fn main() -> int { let a: int = 2 + 3 * 4; let dead: int = 5 * 1; print(a); return 0; }";
    bool ok;
    auto m0 = build(src, false, ok); int before = ir_instr_count(m0);
    auto m1 = build(src, true, ok);  int after  = ir_instr_count(m1);
    CHECK(ok);
    CHECK(after < before);
    // The dead variable's store and the whole arithmetic chain are gone;
    // what remains is essentially `print 14; ret 0`.
    auto* main = find_fn(m1, "main");
    CHECK_EQ(count_op(*main, Op::Mul), 0);
    CHECK_EQ(count_op(*main, Op::Store), 0);   // both locals optimized out
}

static void test_loop_redundant_loads() {
    // Local redundant-load elimination fires inside a loop body: `b` and the
    // temp `t` are each loaded twice from the same slot with no intervening
    // store, so the second loads are removed. (Soundness -- that the loop still
    // computes the same result -- is verified in test_e2e via output equality.)
    const char* src =
        "fn gcd(a: int, b: int) -> int { while (b != 0) { let t: int = b; b = a % b; a = t; } return a; }\n"
        "fn main() -> int { print(gcd(48, 36)); return 0; }";
    bool ok;
    auto m0 = build(src, false, ok); int before = ir_instr_count(m0);
    auto m1 = build(src, true, ok);  int after  = ir_instr_count(m1);
    CHECK(ok);
    CHECK(after < before);   // redundant loads eliminated
    CHECK(after > 0);        // but the loop is still there, not collapsed
}

static void test_fixpoint_convergence() {
    // Nested constants require several fold+prop rounds; pipeline must converge.
    bool ok; auto m = build("fn main() -> int { return ((1+2)*(3+4)) - (5*2); }", true, ok);
    CHECK(ok);
    auto* main = find_fn(m, "main");
    auto* r = find_ret(*main);
    // (3*7) - 10 = 11
    CHECK(r && r->args[0].is_const() && r->args[0].imm == 11);
}

int main() {
    test_constant_folding();
    test_algebraic_times_zero();
    test_algebraic_times_one();
    test_dead_code_removed();
    test_loop_redundant_loads();
    test_fixpoint_convergence();
    TEST_SUMMARY("opt");
}
