#include "pass.h"
#include "ir_util.h"
#include <unordered_map>
#include <unordered_set>

namespace dsl {
namespace {

// Drop blocks not reachable from entry, renumbering the survivors and fixing
// every branch target. (Unreachable blocks appear as empty merge blocks when
// both arms of an `if` return, etc.)
bool remove_unreachable(IRFunction& f) {
    std::vector<char> seen = reachable_blocks(f);
    bool all = true;
    for (char c : seen) if (!c) { all = false; break; }
    if (all) return false;

    std::vector<int> remap(f.blocks.size(), -1);
    std::vector<BasicBlock> keep;
    for (size_t i = 0; i < f.blocks.size(); ++i)
        if (seen[i]) { remap[i] = (int)keep.size(); keep.push_back(std::move(f.blocks[i])); }

    for (size_t i = 0; i < keep.size(); ++i) {
        keep[i].id = (int)i;
        Instr& t = keep[i].instrs.back();
        if (t.op == Op::Br)     t.bb_true = remap[t.bb_true];
        else if (t.op == Op::CondBr) { t.bb_true = remap[t.bb_true]; t.bb_false = remap[t.bb_false]; }
    }
    f.blocks = std::move(keep);
    return true;
}

// Remove pure instructions whose result temp is never used. Iterated to a local
// fixpoint because deleting one instr can make its operands' defs dead too.
bool eliminate_dead_temps(IRFunction& f) {
    bool any = false;
    for (;;) {
        auto uses = count_temp_uses(f);
        bool round = false;
        for (auto& bb : f.blocks)
            for (auto& in : bb.instrs) {
                if (in.dead || in.dst < 0) continue;
                if (op_has_side_effect(in.op)) continue;      // keep Store/Print/Call/terminators
                auto it = uses.find(in.dst);
                if (it == uses.end() || it->second == 0) { in.dead = true; round = true; }
            }
        if (!round) break;
        sweep_dead(f);
        any = true;
    }
    return any;
}

// Dead store elimination -- deliberately conservative (see docs/SPEC.md sec.6):
//   (a) global: a store to a slot that is never loaded ANYWHERE is dead;
//   (b) local:  a store overwritten by a later store to the same slot in the
//       same block, with no intervening load, is dead.
// The fully general version needs backward liveness analysis; getting it wrong
// silently corrupts programs, so we ship only these provably-safe cases.
bool dead_store_elim(IRFunction& f) {
    bool changed = false;

    std::unordered_set<int> loaded;
    for (auto& bb : f.blocks)
        for (auto& in : bb.instrs)
            if (in.op == Op::Load) loaded.insert(in.slot);

    // (a) globally-unused slots
    for (auto& bb : f.blocks)
        for (auto& in : bb.instrs)
            if (in.op == Op::Store && !in.dead && loaded.find(in.slot) == loaded.end()) {
                in.dead = true; changed = true;
            }

    // (b) block-local redundant stores
    for (auto& bb : f.blocks) {
        std::unordered_map<int, size_t> pending;   // slot -> index of a live, not-yet-read store
        for (size_t i = 0; i < bb.instrs.size(); ++i) {
            Instr& in = bb.instrs[i];
            if (in.dead) continue;
            if (in.op == Op::Load) {
                pending.erase(in.slot);             // the pending store is observed; keep it
            } else if (in.op == Op::Store) {
                auto it = pending.find(in.slot);
                if (it != pending.end()) { bb.instrs[it->second].dead = true; changed = true; }
                pending[in.slot] = i;
            }
            // Calls don't read our slots -> pending stores survive across them.
        }
    }

    if (changed) sweep_dead(f);
    return changed;
}

struct DCE : Pass {
    const char* name() const override { return "dce"; }
    bool run(IRFunction& f) override {
        bool a = remove_unreachable(f);
        bool b = eliminate_dead_temps(f);
        bool c = dead_store_elim(f);
        return a || b || c;
    }
};

} // namespace

std::unique_ptr<Pass> make_dce() { return std::make_unique<DCE>(); }

} // namespace dsl
