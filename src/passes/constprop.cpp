#include "pass.h"
#include <unordered_map>

namespace dsl {
namespace {

// Constant + copy propagation.
//
//  * Temps are SSA (defined once), so forwarding a temp defined by `const` or
//    `copy` is sound function-wide -- we resolve chains and rewrite every use.
//  * Named slots are NOT SSA, so slot->constant knowledge is only tracked
//    WITHIN a basic block and reset at each block boundary. That is the
//    conservative, always-correct choice; propagating a slot constant across a
//    branch/merge would require real CFG dataflow (see docs/SPEC.md sec.6).
struct ConstProp : Pass {
    const char* name() const override { return "constprop"; }

    bool run(IRFunction& f) override {
        bool changed = false;

        // 1. Collect temp definitions from const/copy instructions.
        std::unordered_map<int, Value> def;
        for (auto& bb : f.blocks)
            for (auto& in : bb.instrs)
                if (in.dst >= 0 && (in.op == Op::Const || in.op == Op::Copy) && !in.args.empty())
                    def[in.dst] = in.args[0];

        // Resolve a value through chains of const/copy defs (acyclic under SSA).
        auto resolve = [&](Value v) {
            int guard = 0;
            while (v.is_temp() && def.count(v.temp) && guard++ < 100000)
                v = def[v.temp];
            return v;
        };
        std::unordered_map<int, Value> tv;   // fully-resolved value per temp
        for (auto& kv : def) tv[kv.first] = resolve(Value::temp_val(kv.first));

        // 2. Walk blocks: rewrite operands, and track each slot's current value
        //    within the block (a const OR a temp). This does both local constant
        //    propagation AND redundant-load elimination:
        //      - a `load @s` after a `store @s, v` (or an earlier load of @s)
        //        with no intervening store to @s yields v, so we forward it;
        //      - reset at each block boundary (no cross-block claims).
        //    Sound because slots don't alias and calls can't touch this frame.
        for (auto& bb : f.blocks) {
            std::unordered_map<int, Value> slotval;   // slot -> value currently held (this block only)
            for (auto& in : bb.instrs) {
                // Forward known temps into every operand.
                for (auto& a : in.args) {
                    if (a.is_temp()) {
                        auto it = tv.find(a.temp);
                        if (it != tv.end() && !(a == it->second)) { a = it->second; changed = true; }
                    }
                }
                if (in.op == Op::Load) {
                    auto it = slotval.find(in.slot);
                    if (it != slotval.end()) {
                        // Known value in this slot -> replace the load.
                        Value v = it->second;
                        if (v.is_const()) {
                            in.op = Op::Const; in.slot = -1;
                            in.args.assign(1, v);
                        } else {
                            in.op = Op::Copy; in.slot = -1;
                            in.args.assign(1, v);
                        }
                        tv[in.dst] = v; def[in.dst] = v;   // forward this def onward
                        changed = true;
                    } else {
                        // First load of this slot in the block: now we know the
                        // slot currently holds this loaded temp's value.
                        slotval[in.slot] = Value::temp_val(in.dst);
                    }
                } else if (in.op == Op::Store) {
                    // Operand already forwarded above; record what the slot holds.
                    if (!in.args.empty()) slotval[in.slot] = in.args[0];
                    else slotval.erase(in.slot);
                }
                // Op::Call: slots can't be touched by a callee -> keep slotval.
            }
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<Pass> make_constprop() { return std::make_unique<ConstProp>(); }

} // namespace dsl
