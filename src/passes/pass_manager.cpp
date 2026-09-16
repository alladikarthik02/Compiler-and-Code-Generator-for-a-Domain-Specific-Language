#include "pass.h"

namespace dsl {

int PassManager::run(IRFunction& f) {
    int total = 0;
    for (int iter = 0; iter < max_iters; ++iter) {
        bool any = false;
        for (auto& p : passes_) {
            bool changed = p->run(f);
            any = any || changed;
            if (changed) ++total;
        }
        if (!any) break;   // reached a fixpoint
    }
    return total;
}

std::unique_ptr<PassManager> default_pipeline() {
    auto pm = std::make_unique<PassManager>();
    pm->add(make_constfold());
    pm->add(make_algebraic());
    pm->add(make_constprop());
    pm->add(make_dce());
    return pm;
}

} // namespace dsl
