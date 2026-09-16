#include "test.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "irgen.h"
#include "ir.h"
using namespace dsl;

static IRModule build_ir(const std::string& src, bool& ok) {
    Lexer l(src);
    Parser p(l.tokenize());
    auto prog = p.parse_program();
    Sema s;
    ok = p.ok() && s.check(*prog);
    IRGen g;
    return g.generate(*prog);
}

// Structural invariant every well-formed CFG must satisfy: each basic block
// ends in exactly one terminator and has none in the middle.
static bool cfg_well_formed(const IRModule& m) {
    for (auto& f : m.funcs) {
        for (auto& bb : f.blocks) {
            if (bb.instrs.empty()) return false;
            for (size_t i = 0; i < bb.instrs.size(); ++i) {
                bool term = op_is_terminator(bb.instrs[i].op);
                bool last = (i + 1 == bb.instrs.size());
                if (term != last) return false;   // terminator iff last
            }
            // Branch targets must be in range.
            const Instr& t = bb.instrs.back();
            if (t.op == Op::Br && (t.bb_true < 0 || t.bb_true >= (int)f.blocks.size())) return false;
            if (t.op == Op::CondBr) {
                if (t.bb_true < 0 || t.bb_true >= (int)f.blocks.size()) return false;
                if (t.bb_false < 0 || t.bb_false >= (int)f.blocks.size()) return false;
            }
        }
    }
    return true;
}

static void test_wellformed_various() {
    const char* progs[] = {
        "fn main() -> int { return 1 + 2; }",
        "fn main() -> int { let x: int = 3; if (x < 5) { print(x); } else { print(0); } return x; }",
        "fn main() -> int { let x: int = 5; while (x > 0) { x = x - 1; } return x; }",
        "fn main() -> int { let b: bool = true && (1 < 2); if (b) { return 1; } return 0; }",
        ("fn gcd(a: int, b: int) -> int { while (b != 0) { let t: int = b; b = a % b; a = t; } return a; }\n"
         "fn main() -> int { print(gcd(48, 36)); return 0; }"),
    };
    for (auto* src : progs) {
        bool ok; IRModule m = build_ir(src, ok);
        CHECK(ok);
        CHECK(cfg_well_formed(m));
    }
}

static void test_arith_shape() {
    bool ok; IRModule m = build_ir("fn main() -> int { return 2 + 3 * 4; }", ok);
    CHECK(ok);
    // Two binary ops (a mul and an add) on constant operands, plus a ret.
    int muls = 0, adds = 0, rets = 0;
    for (auto& bb : m.funcs[0].blocks)
        for (auto& in : bb.instrs) {
            if (in.op == Op::Mul) muls++;
            if (in.op == Op::Add) adds++;
            if (in.op == Op::Ret) rets++;
        }
    CHECK_EQ(muls, 1);
    CHECK_EQ(adds, 1);
    CHECK_EQ(rets, 1);
}

static void test_short_circuit_uses_branches() {
    bool ok; IRModule m = build_ir(
        "fn main() -> int { let b: bool = (1 < 2) && (3 < 4); if (b) { return 1; } return 0; }", ok);
    CHECK(ok);
    // Short-circuit must NOT emit an 'and' op; it lowers to control flow.
    int ands = 0, condbrs = 0;
    for (auto& bb : m.funcs[0].blocks)
        for (auto& in : bb.instrs) {
            if (in.op == Op::And) ands++;
            if (in.op == Op::CondBr) condbrs++;
        }
    CHECK_EQ(ands, 0);
    CHECK(condbrs >= 2);   // one for &&, one for the if
}

int main() {
    test_wellformed_various();
    test_arith_shape();
    test_short_circuit_uses_branches();
    TEST_SUMMARY("ir");
}
