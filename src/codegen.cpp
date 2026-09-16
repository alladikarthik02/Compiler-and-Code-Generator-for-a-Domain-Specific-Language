#include "codegen.h"
#include <unordered_map>
#include <sstream>

namespace dsl {
namespace {

BOp bin_bop(Op op) {
    switch (op) {
        case Op::Add: return BOp::Add; case Op::Sub: return BOp::Sub; case Op::Mul: return BOp::Mul;
        case Op::Div: return BOp::Div; case Op::Mod: return BOp::Mod; case Op::Lt: return BOp::Lt;
        case Op::Le: return BOp::Le; case Op::Gt: return BOp::Gt; case Op::Ge: return BOp::Ge;
        case Op::Eq: return BOp::Eq; case Op::Ne: return BOp::Ne; case Op::And: return BOp::And;
        case Op::Or: return BOp::Or; default: return BOp::Add;
    }
}
Operand oper(const Value& v) { return v.is_const() ? Operand::c(v.imm) : Operand::r(v.temp); }

BCFunction lower_fn(const IRFunction& f, const std::unordered_map<std::string,int>& fn_index) {
    BCFunction bf;
    bf.name = f.name;
    bf.num_regs = f.next_temp;
    bf.num_slots = f.num_slots;
    bf.num_params = f.num_params;

    // Lay blocks out in id order; remember where each block starts.
    std::vector<int> block_start(f.blocks.size(), 0);
    for (const auto& bb : f.blocks) {
        block_start[bb.id] = (int)bf.code.size();
        for (const auto& in : bb.instrs) {
            switch (in.op) {
                case Op::Const: { BC c; c.op = BOp::LoadConst; c.dst = in.dst; c.a = Operand::c(in.args[0].imm); bf.code.push_back(c); break; }
                case Op::Copy:  { BC c; c.op = BOp::Move; c.dst = in.dst; c.a = oper(in.args[0]); bf.code.push_back(c); break; }
                case Op::Neg:   { BC c; c.op = BOp::Neg; c.dst = in.dst; c.a = oper(in.args[0]); bf.code.push_back(c); break; }
                case Op::Not:   { BC c; c.op = BOp::Not; c.dst = in.dst; c.a = oper(in.args[0]); bf.code.push_back(c); break; }
                case Op::Load:  { BC c; c.op = BOp::LoadSlot; c.dst = in.dst; c.slot_idx = in.slot; bf.code.push_back(c); break; }
                case Op::Store: { BC c; c.op = BOp::StoreSlot; c.slot_idx = in.slot; c.a = oper(in.args[0]); bf.code.push_back(c); break; }
                case Op::Print: { BC c; c.op = BOp::Print; c.a = oper(in.args[0]); bf.code.push_back(c); break; }
                case Op::Call:  { BC c; c.op = BOp::Call; c.dst = in.dst; c.callee = fn_index.at(in.callee);
                                  for (auto& a : in.args) c.args.push_back(oper(a)); bf.code.push_back(c); break; }
                case Op::Br:    { BC c; c.op = BOp::Jmp; c.target = in.bb_true; bf.code.push_back(c); break; }
                case Op::CondBr:{ BC j; j.op = BOp::JmpIfFalse; j.a = oper(in.args[0]); j.target = in.bb_false; bf.code.push_back(j);
                                  BC g; g.op = BOp::Jmp; g.target = in.bb_true; bf.code.push_back(g); break; }
                case Op::Ret:   { BC c; c.op = BOp::Ret; if (!in.args.empty()) c.a = oper(in.args[0]); else c.a = Operand::c(0);
                                  bf.code.push_back(c); break; }
                default: break;
                case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
                case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: case Op::Eq: case Op::Ne:
                case Op::And: case Op::Or: {
                    BC c; c.op = bin_bop(in.op); c.dst = in.dst;
                    c.a = oper(in.args[0]); c.b = oper(in.args[1]); bf.code.push_back(c); break;
                }
            }
        }
    }

    // Patch block-id jump targets to absolute instruction indices.
    for (auto& c : bf.code)
        if (c.op == BOp::Jmp || c.op == BOp::JmpIfFalse)
            c.target = block_start[c.target];

    return bf;
}

} // namespace

BCProgram codegen(const IRModule& m) {
    BCProgram p;
    std::unordered_map<std::string,int> fn_index;
    for (size_t i = 0; i < m.funcs.size(); ++i) fn_index[m.funcs[i].name] = (int)i;
    for (size_t i = 0; i < m.funcs.size(); ++i) {
        p.funcs.push_back(lower_fn(m.funcs[i], fn_index));
        if (m.funcs[i].name == "main") p.main_index = (int)i;
    }
    return p;
}

static const char* bop_str(BOp op) {
    switch (op) {
        case BOp::LoadConst: return "loadc"; case BOp::Move: return "move";
        case BOp::Add: return "add"; case BOp::Sub: return "sub"; case BOp::Mul: return "mul";
        case BOp::Div: return "div"; case BOp::Mod: return "mod"; case BOp::Lt: return "lt";
        case BOp::Le: return "le"; case BOp::Gt: return "gt"; case BOp::Ge: return "ge";
        case BOp::Eq: return "eq"; case BOp::Ne: return "ne"; case BOp::And: return "and";
        case BOp::Or: return "or"; case BOp::Neg: return "neg"; case BOp::Not: return "not";
        case BOp::LoadSlot: return "loads"; case BOp::StoreSlot: return "stores";
        case BOp::Print: return "print"; case BOp::Jmp: return "jmp"; case BOp::JmpIfFalse: return "jmpf";
        case BOp::Call: return "call"; case BOp::Ret: return "ret";
    }
    return "?";
}
static std::string ostr(const Operand& o) {
    return o.imm ? std::to_string(o.val) : ("r" + std::to_string(o.reg));
}

std::string bc_to_string(const BCProgram& p) {
    std::ostringstream os;
    for (auto& f : p.funcs) {
        os << "fn " << f.name << " (regs=" << f.num_regs << ", slots=" << f.num_slots
           << ", params=" << f.num_params << ")\n";
        for (size_t i = 0; i < f.code.size(); ++i) {
            const BC& c = f.code[i];
            os << "  " << i << ": " << bop_str(c.op);
            if (c.dst >= 0 && c.op != BOp::StoreSlot) os << " r" << c.dst << " <-";
            switch (c.op) {
                case BOp::LoadConst: os << " " << c.a.val; break;
                case BOp::Move: case BOp::Neg: case BOp::Not: os << " " << ostr(c.a); break;
                case BOp::LoadSlot: os << " @" << c.slot_idx; break;
                case BOp::StoreSlot: os << " @" << c.slot_idx << ", " << ostr(c.a); break;
                case BOp::Print: os << " " << ostr(c.a); break;
                case BOp::Jmp: os << " ->" << c.target; break;
                case BOp::JmpIfFalse: os << " " << ostr(c.a) << " ->" << c.target; break;
                case BOp::Ret: os << " " << ostr(c.a); break;
                case BOp::Call: {
                    os << " " << c.callee << "(";
                    for (size_t k = 0; k < c.args.size(); ++k) { if (k) os << ", "; os << ostr(c.args[k]); }
                    os << ")"; break;
                }
                default: os << " " << ostr(c.a) << ", " << ostr(c.b); break;
            }
            os << "\n";
        }
    }
    return os.str();
}

} // namespace dsl
