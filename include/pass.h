#pragma once
#include "ir.h"
#include <memory>
#include <vector>

namespace dsl {

// A transform over a single IR function. run() returns true iff it changed
// anything, which drives the fixpoint loop in PassManager.
struct Pass {
    virtual ~Pass() = default;
    virtual const char* name() const = 0;
    virtual bool run(IRFunction& f) = 0;
};

class PassManager {
public:
    void add(std::unique_ptr<Pass> p) { passes_.push_back(std::move(p)); }

    // Runs the pipeline over one function repeatedly until no pass reports a
    // change (a fixpoint) or the iteration cap is hit. Returns total #changes.
    int run(IRFunction& f);
    void run(IRModule& m) { for (auto& f : m.funcs) run(f); }

    int max_iters = 50;

private:
    std::vector<std::unique_ptr<Pass>> passes_;
};

// Pass factories.
std::unique_ptr<Pass> make_constfold();
std::unique_ptr<Pass> make_algebraic();
std::unique_ptr<Pass> make_constprop();
std::unique_ptr<Pass> make_dce();

// The default -O1 pipeline: fold -> simplify -> propagate -> eliminate.
std::unique_ptr<PassManager> default_pipeline();

} // namespace dsl
