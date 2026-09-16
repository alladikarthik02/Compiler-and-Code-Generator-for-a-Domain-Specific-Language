#include "ir.h"
#include <sstream>

namespace dsl {

const char* op_str(Op op) {
    switch (op) {
        case Op::Const: return "const"; case Op::Copy: return "copy";
        case Op::Add: return "add"; case Op::Sub: return "sub";
        case Op::Mul: return "mul"; case Op::Div: return "div"; case Op::Mod: return "mod";
        case Op::Lt: return "lt"; case Op::Le: return "le"; case Op::Gt: return "gt";
        case Op::Ge: return "ge"; case Op::Eq: return "eq"; case Op::Ne: return "ne";
        case Op::And: return "and"; case Op::Or: return "or"; case Op::Neg: return "neg";
        case Op::Not: return "not"; case Op::Load: return "load"; case Op::Call: return "call";
        case Op::Store: return "store"; case Op::Print: return "print"; case Op::Br: return "br";
        case Op::CondBr: return "condbr"; case Op::Ret: return "ret";
    }
    return "?";
}

bool op_is_terminator(Op op) {
    return op == Op::Br || op == Op::CondBr || op == Op::Ret;
}

bool op_has_side_effect(Op op) {
    return op == Op::Store || op == Op::Print || op == Op::Call || op_is_terminator(op);
}

static std::string val_str(const Value& v) {
    if (v.is_const()) return std::to_string(v.imm);
    return "%" + std::to_string(v.temp);
}

std::string ir_to_string(const IRFunction& f) {
    std::ostringstream os;
    os << "fn " << f.name << " (params=" << f.num_params << ", slots=" << f.num_slots << ")\n";
    for (const auto& bb : f.blocks) {
        os << bb.label << ":\n";
        for (const auto& in : bb.instrs) {
            if (in.dead) continue;
            os << "  ";
            if (in.dst >= 0) os << "%" << in.dst << " = ";
            os << op_str(in.op);
            switch (in.op) {
                case Op::Const:
                    os << " " << in.args[0].imm; break;
                case Op::Load:
                    os << " @" << in.slot; break;
                case Op::Store:
                    os << " @" << in.slot << ", " << val_str(in.args[0]); break;
                case Op::Call:
                    os << " " << in.callee << "(";
                    for (size_t i = 0; i < in.args.size(); ++i) { if (i) os << ", "; os << val_str(in.args[i]); }
                    os << ")";
                    break;
                case Op::Br:
                    os << " ." << in.bb_true; break;
                case Op::CondBr:
                    os << " " << val_str(in.args[0]) << ", ." << in.bb_true << ", ." << in.bb_false; break;
                case Op::Ret:
                    if (!in.args.empty()) os << " " << val_str(in.args[0]); break;
                default:
                    for (size_t i = 0; i < in.args.size(); ++i) os << (i ? ", " : " ") << val_str(in.args[i]);
                    break;
            }
            os << "\n";
        }
    }
    return os.str();
}

std::string ir_to_string(const IRModule& m) {
    std::ostringstream os;
    for (const auto& f : m.funcs) os << ir_to_string(f) << "\n";
    return os.str();
}

int ir_instr_count(const IRFunction& f) {
    int n = 0;
    for (const auto& bb : f.blocks)
        for (const auto& in : bb.instrs)
            if (!in.dead) ++n;
    return n;
}

int ir_instr_count(const IRModule& m) {
    int n = 0;
    for (const auto& f : m.funcs) n += ir_instr_count(f);
    return n;
}

} // namespace dsl
