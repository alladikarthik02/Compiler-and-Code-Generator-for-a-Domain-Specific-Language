#include "irgen.h"

namespace dsl {

static Op binop_to_ir(BinOp op) {
    switch (op) {
        case BinOp::Add: return Op::Add; case BinOp::Sub: return Op::Sub;
        case BinOp::Mul: return Op::Mul; case BinOp::Div: return Op::Div;
        case BinOp::Mod: return Op::Mod; case BinOp::Lt: return Op::Lt;
        case BinOp::Le: return Op::Le; case BinOp::Gt: return Op::Gt;
        case BinOp::Ge: return Op::Ge; case BinOp::Eq: return Op::Eq;
        case BinOp::Ne: return Op::Ne; case BinOp::And: return Op::And;
        case BinOp::Or: return Op::Or;
    }
    return Op::Add;
}

IRModule IRGen::generate(const Program& prog) {
    IRModule m;
    for (auto& fn : prog.functions) m.funcs.push_back(gen_function(*fn));
    return m;
}

int IRGen::new_block(const std::string& label) {
    int id = (int)f_->blocks.size();
    BasicBlock bb;
    bb.id = id;
    bb.label = label + "." + std::to_string(id);
    f_->blocks.push_back(std::move(bb));
    return id;
}

Instr& IRGen::emit(Instr in) {
    f_->blocks[cur_].instrs.push_back(std::move(in));
    return f_->blocks[cur_].instrs.back();
}

void IRGen::emit_terminator(Instr in) {
    if (terminated_) return;      // block already ends; ignore stray terminator
    emit(std::move(in));
    terminated_ = true;
}

int IRGen::declare_slot(const std::string& name) {
    int slot = f_->new_slot();
    scopes_.back()[name] = slot;
    return slot;
}
int IRGen::lookup_slot(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return f->second;
    }
    return -1;   // Sema guarantees this never happens
}

IRFunction IRGen::gen_function(const Function& fn) {
    IRFunction f;
    f_ = &f;
    f.name = fn.name;
    f.num_params = (int)fn.params.size();
    scopes_.clear();
    label_ctr_ = 0;
    push_scope();
    // Params occupy the first slots, in order.
    for (auto& p : fn.params) declare_slot(p.name);

    int entry = new_block("entry");
    set_cur(entry);
    gen_block(*fn.body);
    pop_scope();

    finalize_terminators();
    f_ = nullptr;
    return f;
}

void IRGen::finalize_terminators() {
    // Any block that never got a terminator (e.g. an unreachable merge block)
    // gets an implicit `ret 0` so the IR / bytecode stay well-formed.
    for (auto& bb : f_->blocks) {
        if (bb.instrs.empty() || !op_is_terminator(bb.instrs.back().op)) {
            Instr r; r.op = Op::Ret; r.args.push_back(Value::const_val(0));
            bb.instrs.push_back(std::move(r));
        }
    }
}

void IRGen::gen_block(const Block& b) {
    push_scope();
    for (auto& s : b.stmts) {
        if (terminated_) break;   // rest is unreachable
        gen_stmt(*s);
    }
    pop_scope();
}

void IRGen::gen_stmt(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Let: {
            auto& l = static_cast<const LetStmt&>(s);
            Value v = gen_expr(*l.init);
            int slot = declare_slot(l.name);
            Instr st; st.op = Op::Store; st.slot = slot; st.args.push_back(v);
            emit(std::move(st));
            break;
        }
        case StmtKind::Assign: {
            auto& a = static_cast<const AssignStmt&>(s);
            Value v = gen_expr(*a.value);
            int slot = lookup_slot(a.name);
            Instr st; st.op = Op::Store; st.slot = slot; st.args.push_back(v);
            emit(std::move(st));
            break;
        }
        case StmtKind::Print: {
            auto& p = static_cast<const PrintStmt&>(s);
            Value v = gen_expr(*p.value);
            Instr pr; pr.op = Op::Print; pr.args.push_back(v);
            emit(std::move(pr));
            break;
        }
        case StmtKind::Return: {
            auto& r = static_cast<const ReturnStmt&>(s);
            Value v = gen_expr(*r.value);
            Instr rt; rt.op = Op::Ret; rt.args.push_back(v);
            emit_terminator(std::move(rt));
            break;
        }
        case StmtKind::ExprStmt: {
            auto& e = static_cast<const ExprStmt&>(s);
            gen_expr(*e.expr);   // evaluated for side effects (e.g. call)
            break;
        }
        case StmtKind::Block:
            gen_block(static_cast<const Block&>(s));
            break;
        case StmtKind::If: {
            auto& i = static_cast<const IfStmt&>(s);
            Value c = gen_expr(*i.cond);
            int thenBB = new_block("then");
            int elseBB = i.else_blk ? new_block("else") : -1;
            int mergeBB = new_block("merge");

            Instr cb; cb.op = Op::CondBr; cb.args.push_back(c);
            cb.bb_true = thenBB; cb.bb_false = (elseBB >= 0 ? elseBB : mergeBB);
            emit_terminator(std::move(cb));

            set_cur(thenBB);
            gen_block(*i.then_blk);
            { Instr br; br.op = Op::Br; br.bb_true = mergeBB; emit_terminator(std::move(br)); }

            if (elseBB >= 0) {
                set_cur(elseBB);
                gen_block(*i.else_blk);
                Instr br; br.op = Op::Br; br.bb_true = mergeBB; emit_terminator(std::move(br));
            }
            set_cur(mergeBB);
            break;
        }
        case StmtKind::While: {
            auto& w = static_cast<const WhileStmt&>(s);
            int headerBB = new_block("while.header");
            { Instr br; br.op = Op::Br; br.bb_true = headerBB; emit_terminator(std::move(br)); }

            set_cur(headerBB);
            Value c = gen_expr(*w.cond);      // condition re-evaluated each iteration
            int bodyBB = new_block("while.body");
            int exitBB = new_block("while.exit");
            Instr cb; cb.op = Op::CondBr; cb.args.push_back(c);
            cb.bb_true = bodyBB; cb.bb_false = exitBB;
            emit_terminator(std::move(cb));

            set_cur(bodyBB);
            gen_block(*w.body);
            { Instr br; br.op = Op::Br; br.bb_true = headerBB; emit_terminator(std::move(br)); }

            set_cur(exitBB);
            break;
        }
    }
}

Value IRGen::gen_expr(const Expr& e) {
    switch (e.kind) {
        case ExprKind::IntLit:
            return Value::const_val(static_cast<const IntLit&>(e).value);
        case ExprKind::BoolLit:
            return Value::const_val(static_cast<const BoolLit&>(e).value ? 1 : 0);
        case ExprKind::Var: {
            auto& v = static_cast<const VarExpr&>(e);
            int slot = lookup_slot(v.name);
            Instr ld; ld.op = Op::Load; ld.slot = slot; ld.dst = f_->new_temp();
            emit(std::move(ld));
            return Value::temp_val(f_->blocks[cur_].instrs.back().dst);
        }
        case ExprKind::Unary: {
            auto& u = static_cast<const UnaryExpr&>(e);
            Value o = gen_expr(*u.operand);
            Instr in; in.op = (u.op == UnOp::Neg) ? Op::Neg : Op::Not;
            in.dst = f_->new_temp(); in.args.push_back(o);
            emit(std::move(in));
            return Value::temp_val(f_->blocks[cur_].instrs.back().dst);
        }
        case ExprKind::Binary: {
            auto& b = static_cast<const BinaryExpr&>(e);
            if (b.op == BinOp::And || b.op == BinOp::Or)
                return gen_short_circuit(b);
            Value l = gen_expr(*b.lhs);
            Value r = gen_expr(*b.rhs);
            Instr in; in.op = binop_to_ir(b.op); in.dst = f_->new_temp();
            in.args.push_back(l); in.args.push_back(r);
            emit(std::move(in));
            return Value::temp_val(f_->blocks[cur_].instrs.back().dst);
        }
        case ExprKind::Call: {
            auto& c = static_cast<const CallExpr&>(e);
            std::vector<Value> argv;
            for (auto& a : c.args) argv.push_back(gen_expr(*a));
            Instr in; in.op = Op::Call; in.callee = c.callee; in.dst = f_->new_temp();
            in.args = std::move(argv);
            emit(std::move(in));
            return Value::temp_val(f_->blocks[cur_].instrs.back().dst);
        }
    }
    return Value::const_val(0);
}

// Short-circuit && / || lowered to a branch diamond writing a scratch slot,
// which the merge block reads back. (No phi nodes -> use memory.)
Value IRGen::gen_short_circuit(const BinaryExpr& b) {
    int scratch = f_->new_slot();
    Value l = gen_expr(*b.lhs);

    int rhsBB   = new_block("sc.rhs");
    int shortBB = new_block("sc.short");   // the short-circuit path
    int mergeBB = new_block("sc.merge");

    Instr cb; cb.op = Op::CondBr; cb.args.push_back(l);
    if (b.op == BinOp::And) { cb.bb_true = rhsBB; cb.bb_false = shortBB; }  // false short-circuits
    else                    { cb.bb_true = shortBB; cb.bb_false = rhsBB; }  // true  short-circuits
    emit_terminator(std::move(cb));

    // short path: result is the short-circuit constant (0 for &&, 1 for ||)
    set_cur(shortBB);
    { Instr st; st.op = Op::Store; st.slot = scratch;
      st.args.push_back(Value::const_val(b.op == BinOp::And ? 0 : 1));
      emit(std::move(st)); }
    { Instr br; br.op = Op::Br; br.bb_true = mergeBB; emit_terminator(std::move(br)); }

    // rhs path: result is the rhs value
    set_cur(rhsBB);
    Value r = gen_expr(*b.rhs);
    { Instr st; st.op = Op::Store; st.slot = scratch; st.args.push_back(r); emit(std::move(st)); }
    { Instr br; br.op = Op::Br; br.bb_true = mergeBB; emit_terminator(std::move(br)); }

    set_cur(mergeBB);
    Instr ld; ld.op = Op::Load; ld.slot = scratch; ld.dst = f_->new_temp();
    emit(std::move(ld));
    return Value::temp_val(f_->blocks[cur_].instrs.back().dst);
}

} // namespace dsl
