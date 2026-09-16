# `dslc` — a compiler & code generator for a small DSL

A from-scratch optimizing compiler in C++17 for a small statically-typed imperative language. It owns the **entire** pipeline — lexer, parser, semantic analysis, a custom three-address IR, a pluggable optimization-pass framework, a register bytecode backend, and a VM that executes the result. No LLVM, no parser generators, no external libraries.

```
source.dsl → lexer → parser → sema → IR → [optimizer] → bytecode → VM → output
```

## Build & test

```sh
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires a C++17 compiler and CMake ≥ 3.16. (On macOS with a partial Command Line Tools install, CMake auto-detects and points the compiler at the SDK's libc++ headers — see the top of `CMakeLists.txt`.)

## Run

```sh
./build/dslc examples/fib.dsl            # compile at -O1 and execute
./build/dslc examples/gcd.dsl --stats    # ...and report instruction counts
./build/dslc file.dsl --emit=tokens      # inspect any pipeline stage:
./build/dslc file.dsl --emit=ast         #   tokens | ast | ir | ir-opt | bc
./build/dslc file.dsl --emit=ir-opt -O0  # disable the optimizer
```

Every arrow in the pipeline is directly observable via `--emit`, which is both the debugging surface and the demo surface.

## The language

Statically typed, two types (`int` = 64-bit signed, `bool`), functions, `if/else`, `while`, `let`/assignment, `print`, recursion, and short-circuiting `&&`/`||`.

```dsl
fn gcd(a: int, b: int) -> int {
  while (b != 0) {
    let t: int = b;
    b = a % b;
    a = t;
  }
  return a;
}

fn main() -> int {
  print(gcd(48, 36));   // 12
  return 0;
}
```

See `docs/SPEC.md` for the full grammar, operator-precedence table, and type rules.

## Architecture

| Stage | Files | What it does |
|-------|-------|--------------|
| Lexer | `lexer.{h,cpp}`, `token.h` | Hand-written scanner; tracks line/col; maximal-munch for multi-char operators. |
| Parser | `parser.{h,cpp}`, `ast.h` | Recursive descent + **Pratt** expression parsing; panic-mode error recovery. |
| Sema | `sema.{h,cpp}` | Scoped symbol tables, type checking, arity checks, "all paths return", poison-type cascade suppression. |
| IR | `ir.{h}`, `ir.cpp`, `irgen.{h,cpp}` | Lowers AST to a linear three-address IR (functions → basic blocks → instructions); control flow and short-circuit `&&`/`||` become branch diamonds. |
| Optimizer | `pass.h`, `passes/*.cpp` | Pass framework iterated to a fixpoint: constant folding, algebraic simplification, constant/copy propagation + redundant-load elimination, dead-code elimination. |
| Backend | `bytecode.h`, `codegen.{h,cpp}`, `vm.{h,cpp}` | Lowers optimized IR to register bytecode; a small VM executes it (traps on divide-by-zero and stack overflow). |

## Optimizations

The pass pipeline (`default_pipeline()`), run to a fixpoint:

1. **Constant folding** — `add 3, 4 → 7`; never folds divide/modulo by zero (left to trap at runtime).
2. **Algebraic simplification** — `x+0→x`, `x*1→x`, `x*0→0`, `x-x→0`, `x/1→x`, `x%1→0`, … (all exact for two's-complement integers).
3. **Constant + copy propagation & redundant-load elimination** — temps are SSA (defined once) so temp forwarding is sound function-wide; named-variable values are tracked *within a basic block* to fold `load`s and drop redundant reloads.
4. **Dead-code elimination** — unreachable-block removal (with CFG renumbering), dead-temp elimination, and conservative dead-store elimination.

**Measured code-size reduction** (`--stats`, IR instruction count `-O0 → -O1`):

| Program | Before | After | Removed |
|---------|-------:|------:|--------:|
| `constants.dsl` | 19 | 3 | **84%** |
| `opt_demo.dsl` | 8 | 2 | **75%** |
| `gcd.dsl` | 18 | 15 | **17%** |
| `sum.dsl` | 17 | 16 | 6% |
| `fib.dsl` | 18 | 17 | 6% |

The **governing correctness invariant**: every optimization is verified two ways — the optimizer must reduce instruction count *and* produce byte-identical program output vs. `-O0`. The `-O0 ≡ -O1` output-equality check runs over a corpus in `tests/test_e2e.cpp`.

## Testing

Seven suites (dependency-free harness in `tests/test.h`), run via `ctest`:

- `test_lexer` — token kinds, positions, maximal munch, comments, lex errors.
- `test_parser` — precedence/associativity tree shapes, calls, error recovery.
- `test_sema` — type errors, scoping/shadowing, arity, return-path analysis, `main` signature.
- `test_ir` — CFG well-formedness (each block ends in exactly one terminator), short-circuit lowers to branches.
- `test_opt` — folding/algebraic/DCE reductions, loop redundant-load elimination, fixpoint convergence.
- `test_e2e` — known outputs (fib/fact/gcd/…), **semantic preservation (`-O0 ≡ -O1`)**, short-circuit avoids traps, runtime traps.

## Deliberate scope cuts

Named in `docs/SPEC.md §6` and worth stating up front: no full SSA/phi (variable optimization is per-block, not global); dead-store elimination is the provably-safe local subset; `call` is opaque (no inlining/interprocedural analysis); the backend is a register bytecode VM rather than native codegen. Each is a considered trade-off, not an oversight.
