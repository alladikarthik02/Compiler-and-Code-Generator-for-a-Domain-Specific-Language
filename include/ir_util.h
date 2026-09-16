#pragma once
#include "ir.h"
#include <unordered_map>
#include <vector>

namespace dsl {

// How many times each temp id is used as an operand across the whole function
// (operands in args + CondBr condition). A defined-but-unused temp has count 0.
inline std::unordered_map<int,int> count_temp_uses(const IRFunction& f) {
    std::unordered_map<int,int> uses;
    for (const auto& bb : f.blocks)
        for (const auto& in : bb.instrs)
            for (const auto& a : in.args)
                if (a.is_temp()) uses[a.temp]++;
    return uses;
}

// Marks blocks reachable from the entry (block 0) by following Br/CondBr edges.
inline std::vector<char> reachable_blocks(const IRFunction& f) {
    std::vector<char> seen(f.blocks.size(), 0);
    if (f.blocks.empty()) return seen;
    std::vector<int> stack = {0};
    seen[0] = 1;
    while (!stack.empty()) {
        int b = stack.back(); stack.pop_back();
        const Instr& t = f.blocks[b].instrs.back();
        auto visit = [&](int id) {
            if (id >= 0 && id < (int)seen.size() && !seen[id]) { seen[id] = 1; stack.push_back(id); }
        };
        if (t.op == Op::Br) visit(t.bb_true);
        else if (t.op == Op::CondBr) { visit(t.bb_true); visit(t.bb_false); }
    }
    return seen;
}

// Removes instructions flagged dead within every block. Never removes the
// terminator (passes must not flag those). Returns true if anything was swept.
inline bool sweep_dead(IRFunction& f) {
    bool changed = false;
    for (auto& bb : f.blocks) {
        auto& v = bb.instrs;
        size_t w = 0;
        for (size_t r = 0; r < v.size(); ++r) {
            if (v[r].dead) { changed = true; continue; }
            if (w != r) v[w] = std::move(v[r]);
            ++w;
        }
        v.resize(w);
    }
    return changed;
}

} // namespace dsl
