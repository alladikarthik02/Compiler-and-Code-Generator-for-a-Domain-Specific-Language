#include "pass.h"

namespace dsl {
namespace {

void make_const(Instr& in, int64_t v) {
    in.op = Op::Const; in.args.clear(); in.args.push_back(Value::const_val(v));
    in.slot = -1; in.callee.clear();
}
void make_copy(Instr& in, Value src) {
    in.op = Op::Copy; in.args.clear(); in.args.push_back(src);
    in.slot = -1; in.callee.clear();
}
bool is_const(const Value& v, int64_t c) { return v.is_const() && v.imm == c; }

// Algebraic identities. All are exact for 64-bit two's-complement integers.
// (Note: x*0 -> 0 and x-x -> 0 would NOT be valid for floats, where x could be
//  NaN/Inf -- a good interview distinction. Our only numeric type is int.)
struct Algebraic : Pass {
    const char* name() const override { return "algebraic"; }
    bool run(IRFunction& f) override {
        bool changed = false;
        for (auto& bb : f.blocks) {
            for (auto& in : bb.instrs) {
                if (in.dst < 0 || in.args.size() != 2) continue;
                const Value a = in.args[0], b = in.args[1];
                switch (in.op) {
                    case Op::Add:
                        if (is_const(b, 0)) { make_copy(in, a); changed = true; }
                        else if (is_const(a, 0)) { make_copy(in, b); changed = true; }
                        break;
                    case Op::Sub:
                        if (is_const(b, 0)) { make_copy(in, a); changed = true; }
                        else if (a == b)    { make_const(in, 0); changed = true; }  // x - x
                        break;
                    case Op::Mul:
                        if (is_const(b, 0) || is_const(a, 0)) { make_const(in, 0); changed = true; }
                        else if (is_const(b, 1)) { make_copy(in, a); changed = true; }
                        else if (is_const(a, 1)) { make_copy(in, b); changed = true; }
                        break;
                    case Op::Div:
                        // x/1 -> x. (x/x, 0/x are unsafe: divisor may be 0 -> trap.)
                        if (is_const(b, 1)) { make_copy(in, a); changed = true; }
                        break;
                    case Op::Mod:
                        // x % 1 == 0 for all x. (x%x unsafe: x may be 0.)
                        if (is_const(b, 1)) { make_const(in, 0); changed = true; }
                        break;
                    case Op::And:  // only appears if a non-short-circuit `and` is ever built
                        if (is_const(b, 0) || is_const(a, 0)) { make_const(in, 0); changed = true; }
                        else if (is_const(b, 1)) { make_copy(in, a); changed = true; }
                        else if (is_const(a, 1)) { make_copy(in, b); changed = true; }
                        break;
                    case Op::Or:
                        if (is_const(b, 1) || is_const(a, 1)) { make_const(in, 1); changed = true; }
                        else if (is_const(b, 0)) { make_copy(in, a); changed = true; }
                        else if (is_const(a, 0)) { make_copy(in, b); changed = true; }
                        break;
                    default: break;
                }
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> make_algebraic() { return std::make_unique<Algebraic>(); }

} // namespace dsl
