#include "lexer.h"
#include "parser.h"
#include "ast_print.h"
#include "sema.h"
#include "irgen.h"
#include "ir.h"
#include "pass.h"
#include "codegen.h"
#include "vm.h"
#include <cstdio>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

using namespace dsl;

static std::string read_file(const char* path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { ok = false; return {}; }
    std::ostringstream ss; ss << in.rdbuf();
    ok = true;
    return ss.str();
}

static void usage() {
    std::fprintf(stderr,
        "usage: dslc <file.dsl> [--emit=tokens|ast]\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }

    std::string emit, path;
    bool optimize = true;   // -O1 default
    bool stats = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--emit=", 0) == 0) emit = a.substr(7);
        else if (a == "-O0") optimize = false;
        else if (a == "-O1") optimize = true;
        else if (a == "--stats") stats = true;
        else if (a[0] != '-') path = a;
    }
    if (path.empty()) { usage(); return 2; }

    bool ok = false;
    std::string src = read_file(path.c_str(), ok);
    if (!ok) { std::fprintf(stderr, "error: cannot open %s\n", path.c_str()); return 2; }

    Lexer lex(src, path);
    auto toks = lex.tokenize();
    for (const auto& e : lex.errors())
        std::fprintf(stderr, "%s:%d:%d: error: %s\n", path.c_str(), e.line, e.col, e.message.c_str());
    if (!lex.ok()) return 1;

    if (emit == "tokens") {
        for (const auto& t : toks)
            std::printf("%4d:%-3d %-8s '%s'\n", t.line, t.col, tok_name(t.kind), t.lexeme.c_str());
        return 0;
    }

    Parser parser(std::move(toks), path);
    auto prog = parser.parse_program();
    for (const auto& e : parser.errors())
        std::fprintf(stderr, "%s:%d:%d: error: %s\n", path.c_str(), e.line, e.col, e.message.c_str());
    if (!parser.ok()) return 1;

    if (emit == "ast") {
        std::string s = ast_to_string(*prog);
        std::fputs(s.c_str(), stdout);
        return 0;
    }

    Sema sema;
    bool sema_ok = sema.check(*prog);
    for (const auto& e : sema.errors())
        std::fprintf(stderr, "%s:%d:%d: error: %s\n", path.c_str(), e.line, e.col, e.message.c_str());
    if (!sema_ok) return 1;

    if (emit == "sema") {
        std::fprintf(stderr, "ok: type-checked %zu function(s)\n", prog->functions.size());
        return 0;
    }

    IRGen irgen;
    IRModule mod = irgen.generate(*prog);

    if (emit == "ir") {   // unoptimized IR
        std::fputs(ir_to_string(mod).c_str(), stdout);
        return 0;
    }

    int before = ir_instr_count(mod);
    if (optimize) {
        auto pm = default_pipeline();
        pm->run(mod);
    }
    int after = ir_instr_count(mod);

    if (stats) {
        std::fprintf(stderr, "instructions: %d -> %d  (%.1f%% removed)\n",
                     before, after, before ? 100.0 * (before - after) / before : 0.0);
    }

    if (emit == "ir-opt") {
        std::fputs(ir_to_string(mod).c_str(), stdout);
        return 0;
    }

    BCProgram prog_bc = codegen(mod);

    if (emit == "bc") {
        std::fputs(bc_to_string(prog_bc).c_str(), stdout);
        return 0;
    }

    // Default action: execute on the VM.
    VMResult res = run_program(prog_bc, std::cout);
    if (!res.ok) {
        std::fprintf(stderr, "%s: runtime error: %s\n", path.c_str(), res.error.c_str());
        return 1;
    }
    return 0;
}
