#include "vm.h"
#include <vector>
#include <stdexcept>
#include <cstdint>

namespace dsl {
namespace {

struct Trap { std::string msg; };

// Signed overflow is UB in C++, so arithmetic wraps via unsigned (where
// wraparound is defined). These MUST match the helpers in passes/constfold.cpp:
// a value computed at runtime and the same value folded at compile time have to
// agree, otherwise -O0 and -O1 could produce different output.
inline int64_t wrap_add(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
inline int64_t wrap_sub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
inline int64_t wrap_mul(int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); }
inline int64_t wrap_neg(int64_t a)            { return (int64_t)((uint64_t)0 - (uint64_t)a); }

class VM {
public:
    VM(const BCProgram& p, std::ostream& out) : p_(p), out_(out) {}

    int64_t call(int fn, const std::vector<int64_t>& args) {
        if (++depth_ > kMaxDepth) throw Trap{"stack overflow (recursion too deep)"};
        const BCFunction& f = p_.funcs[fn];
        std::vector<int64_t> regs(f.num_regs, 0);
        std::vector<int64_t> slots(f.num_slots, 0);
        for (size_t i = 0; i < args.size() && i < slots.size(); ++i) slots[i] = args[i];

        auto val = [&](const Operand& o) -> int64_t { return o.imm ? o.val : regs[o.reg]; };

        size_t pc = 0;
        for (;;) {
            const BC& c = f.code[pc];
            switch (c.op) {
                case BOp::LoadConst: regs[c.dst] = c.a.val; ++pc; break;
                case BOp::Move:      regs[c.dst] = val(c.a); ++pc; break;
                case BOp::Add: regs[c.dst] = wrap_add(val(c.a), val(c.b)); ++pc; break;
                case BOp::Sub: regs[c.dst] = wrap_sub(val(c.a), val(c.b)); ++pc; break;
                case BOp::Mul: regs[c.dst] = wrap_mul(val(c.a), val(c.b)); ++pc; break;
                case BOp::Div: {
                    int64_t x = val(c.a), y = val(c.b);
                    if (y == 0) throw Trap{"division by zero"};
                    if (x == INT64_MIN && y == -1) { regs[c.dst] = INT64_MIN; ++pc; break; }
                    regs[c.dst] = x / y; ++pc; break;
                }
                case BOp::Mod: {
                    int64_t x = val(c.a), y = val(c.b);
                    if (y == 0) throw Trap{"modulo by zero"};
                    if (x == INT64_MIN && y == -1) { regs[c.dst] = 0; ++pc; break; }
                    regs[c.dst] = x % y; ++pc; break;
                }
                case BOp::Lt: regs[c.dst] = (val(c.a) <  val(c.b)); ++pc; break;
                case BOp::Le: regs[c.dst] = (val(c.a) <= val(c.b)); ++pc; break;
                case BOp::Gt: regs[c.dst] = (val(c.a) >  val(c.b)); ++pc; break;
                case BOp::Ge: regs[c.dst] = (val(c.a) >= val(c.b)); ++pc; break;
                case BOp::Eq: regs[c.dst] = (val(c.a) == val(c.b)); ++pc; break;
                case BOp::Ne: regs[c.dst] = (val(c.a) != val(c.b)); ++pc; break;
                case BOp::And: regs[c.dst] = (val(c.a) != 0 && val(c.b) != 0); ++pc; break;
                case BOp::Or:  regs[c.dst] = (val(c.a) != 0 || val(c.b) != 0); ++pc; break;
                case BOp::Neg: regs[c.dst] = wrap_neg(val(c.a)); ++pc; break;
                case BOp::Not: regs[c.dst] = (val(c.a) == 0); ++pc; break;
                case BOp::LoadSlot:  regs[c.dst] = slots[c.slot_idx]; ++pc; break;
                case BOp::StoreSlot: slots[c.slot_idx] = val(c.a); ++pc; break;
                case BOp::Print: out_ << val(c.a) << "\n"; ++pc; break;
                case BOp::Jmp: pc = c.target; break;
                case BOp::JmpIfFalse: pc = (val(c.a) == 0) ? (size_t)c.target : pc + 1; break;
                case BOp::Call: {
                    std::vector<int64_t> a;
                    a.reserve(c.args.size());
                    for (const auto& o : c.args) a.push_back(val(o));
                    regs[c.dst] = call(c.callee, a);
                    ++pc; break;
                }
                case BOp::Ret:
                    --depth_;
                    return val(c.a);
            }
        }
    }

private:
    static constexpr int kMaxDepth = 5000;
    const BCProgram& p_;
    std::ostream& out_;
    int depth_ = 0;
};

} // namespace

VMResult run_program(const BCProgram& p, std::ostream& out) {
    VMResult r;
    if (p.main_index < 0) { r.ok = false; r.error = "no main function"; return r; }
    try {
        VM vm(p, out);
        r.value = vm.call(p.main_index, {});
        r.ok = true;
    } catch (const Trap& t) {
        r.ok = false;
        r.error = t.msg;
    }
    return r;
}

} // namespace dsl
