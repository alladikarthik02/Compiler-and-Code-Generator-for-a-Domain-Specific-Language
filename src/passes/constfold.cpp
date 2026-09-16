#include "pass.h"
#include <cstdint>

namespace dsl {
namespace {

// Signed integer overflow is UNDEFINED BEHAVIOUR in C++ -- it is not guaranteed
// wraparound. So all folding arithmetic is performed in unsigned (where
// wraparound *is* defined) and converted back, which yields the two's-complement
// semantics documented in docs/SPEC.md without invoking UB.
// (Found by UBSan: folding `9223372036854775807 + 1` used to be UB here.)
// The VM in src/vm.cpp uses the identical helpers, so a folded constant and a
// runtime computation always agree -- which is what keeps -O0 == -O1 true.
inline int64_t wrap_add(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
inline int64_t wrap_sub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
inline int64_t wrap_mul(int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); }
inline int64_t wrap_neg(int64_t a)            { return (int64_t)((uint64_t)0 - (uint64_t)a); }

// Try to evaluate a binary op on two integer constants. Returns false when the
// result is intentionally left for runtime (division / modulo by zero -> trap).
bool fold_binary(Op op, int64_t a, int64_t b, int64_t& out) {
    switch (op) {
        case Op::Add: out = wrap_add(a, b); return true;
        case Op::Sub: out = wrap_sub(a, b); return true;
        case Op::Mul: out = wrap_mul(a, b); return true;
        // INT64_MIN / -1 overflows (the true result doesn't fit); wraps to INT64_MIN.
        case Op::Div: if (b == 0) return false;
                      if (a == INT64_MIN && b == -1) { out = INT64_MIN; return true; }
                      out = a / b; return true;
        // INT64_MIN % -1 is UB on many platforms; the mathematical result is 0.
        case Op::Mod: if (b == 0) return false;
                      if (a == INT64_MIN && b == -1) { out = 0; return true; }
                      out = a % b; return true;
        case Op::Lt: out = (a <  b); return true;
        case Op::Le: out = (a <= b); return true;
        case Op::Gt: out = (a >  b); return true;
        case Op::Ge: out = (a >= b); return true;
        case Op::Eq: out = (a == b); return true;
        case Op::Ne: out = (a != b); return true;
        case Op::And: out = (a != 0 && b != 0); return true;
        case Op::Or:  out = (a != 0 || b != 0); return true;
        default: return false;
    }
}

// Rewrites an instruction in place into `%dst = const value`.
void make_const(Instr& in, int64_t value) {
    Op saved = in.op;
    (void)saved;
    in.op = Op::Const;
    in.args.clear();
    in.args.push_back(Value::const_val(value));
    in.slot = -1;
    in.callee.clear();
}

struct ConstFold : Pass {
    const char* name() const override { return "constfold"; }
    bool run(IRFunction& f) override {
        bool changed = false;
        for (auto& bb : f.blocks) {
            for (auto& in : bb.instrs) {
                if (in.dst < 0) continue;                 // only value-producing instrs
                if (in.op == Op::Const || in.op == Op::Copy) continue;

                if (in.op == Op::Neg && in.args.size() == 1 && in.args[0].is_const()) {
                    make_const(in, wrap_neg(in.args[0].imm)); changed = true; continue;
                }
                if (in.op == Op::Not && in.args.size() == 1 && in.args[0].is_const()) {
                    make_const(in, in.args[0].imm == 0 ? 1 : 0); changed = true; continue;
                }
                if (in.args.size() == 2 && in.args[0].is_const() && in.args[1].is_const()) {
                    int64_t out;
                    if (fold_binary(in.op, in.args[0].imm, in.args[1].imm, out)) {
                        make_const(in, out); changed = true; continue;
                    }
                }
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> make_constfold() { return std::make_unique<ConstFold>(); }

} // namespace dsl
