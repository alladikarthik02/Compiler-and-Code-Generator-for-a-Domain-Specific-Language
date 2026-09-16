# `dslc` — Tech Spec: A Compiler & Code Generator for a Small DSL

**Author:** Karthik Alladi
**Status:** Draft → building
**Goal:** Build the full pipeline of a compiler for a small domain-specific language in C++: lexer → parser → semantic analysis → custom IR → optimization passes → code generation → execution. Own *every* stage. No LLVM (that's the other project); here the IR, the passes, and the backend are all ours.

---

## 1. What we are building and why

A compiler for a small imperative DSL called **`dsl`** (files end in `.dsl`). It's deliberately small enough to build in a day, but rich enough to write real programs (fibonacci, gcd, factorial, loops) so that:

1. The **optimizations are measurable** — we can show constant folding + dead-code elimination reducing generated instruction count on real programs, *while proving output is unchanged* (semantic preservation).
2. The **pass framework is real** — new optimization/codegen passes plug in without touching the rest of the compiler.
3. The thing **actually runs** — we lower IR to bytecode and execute it on a small virtual machine, so end-to-end tests assert real program output.

### The pipeline

```
 source.dsl
    │
    ▼  Lexer            characters → tokens
 [tokens]
    │
    ▼  Parser (recursive descent + Pratt)   tokens → AST
 [AST]
    │
    ▼  Sema             scopes, symbol tables, type checking → typed AST (or errors)
 [typed AST]
    │
    ▼  IRGen            AST → linear three-address IR (functions → basic blocks → instrs)
 [IR  (unoptimized)]
    │
    ▼  PassManager      constfold → algebraic → constprop → DCE  (iterate to fixpoint)
 [IR  (optimized)]
    │
    ▼  CodeGen          IR → register bytecode
 [bytecode module]
    │
    ▼  VM               execute → program output
 stdout
```

Every arrow is inspectable from the CLI (`--emit=tokens|ast|ir|ir-opt|bc`), which is both a debugging tool and the thing that makes the project *demonstrable* in an interview.

---

## 2. The language

Small, C-like, statically typed. Types: `int` (64-bit signed) and `bool`.

```dsl
// gcd + a driver — a real program the optimizer can chew on
fn gcd(a: int, b: int) -> int {
    while (b != 0) {
        let t: int = b;
        b = a % b;
        a = t;
    }
    return a;
}

fn main() -> int {
    let x: int = 48;
    let y: int = 36;
    print(gcd(x, y));      // 12
    let dead: int = 2 + 3; // never used  -> DCE should remove it
    let k: int = 10 * 1;   // -> algebraic simplification + constfold -> 10
    print(k);
    return 0;
}
```

### Grammar (EBNF, informal)

```
program     := function*
function    := "fn" IDENT "(" params? ")" "->" type block
params      := param ("," param)*
param       := IDENT ":" type
type        := "int" | "bool"
block       := "{" statement* "}"
statement   := letStmt | assignStmt | ifStmt | whileStmt | returnStmt | printStmt | exprStmt
letStmt     := "let" IDENT ":" type "=" expr ";"
assignStmt  := IDENT "=" expr ";"
ifStmt      := "if" "(" expr ")" block ("else" block)?
whileStmt   := "while" "(" expr ")" block
returnStmt  := "return" expr? ";"
printStmt   := "print" "(" expr ")" ";"
exprStmt    := expr ";"
expr        := (Pratt expression parser; see precedence table)
```

### Operators & precedence (lowest → highest)

| Level | Operators            | Assoc  |
|-------|----------------------|--------|
| 1     | `||`                 | left   |
| 2     | `&&`                 | left   |
| 3     | `== !=`              | left   |
| 4     | `< <= > >=`          | left   |
| 5     | `+ -`                | left   |
| 6     | `* / %`              | left   |
| 7     | unary `- !`          | right  |
| 8     | `f(...)`, `( )`      | —      |

Semantics worth pinning down now (these are exactly the "poke a hole" questions):
- `int` is 64-bit signed, two's complement; `/` and `%` are truncated-toward-zero (C++ semantics). Division/mod by zero is a **runtime trap** in the VM (defined behavior, not UB).
- `&&` and `||` **short-circuit**. This matters for both semantics and IR shape (they lower to branches, not to a single instruction).
- `bool` is its own type; you can't do arithmetic on `bool`, and `if(x)` requires `x: bool` (no implicit int→bool). This keeps Sema honest.
- Every non-`void` path must return — we'll do a conservative "all paths return" check.

---

## 3. Stage-by-stage design

### 3.1 Lexer (`src/lexer.cpp`)
Hand-written scanner. Produces a `vector<Token>`. Each token carries `{Kind, lexeme, line, col}` for good diagnostics. Handles `//` comments, integer literals, identifiers/keywords, multi-char operators (`==`, `!=`, `<=`, `>=`, `&&`, `||`, `->`). Emits a final `Eof` token so the parser never reads past the end.

**Debugging decision baked in:** track line/col from the start. Retrofitting source locations after the fact is miserable; every later stage's error messages depend on them.

### 3.2 Parser (`src/parser.cpp`)
Recursive descent for statements/declarations, **Pratt (precedence-climbing)** for expressions — this is the clean way to get the precedence table above without a rat's nest of `parseTerm/parseFactor` functions. Produces an AST (`include/ast.h`) of `Stmt`/`Expr` node structs held by `unique_ptr`.

Error strategy: on a syntax error, report `file:line:col: message`, then **synchronize** to the next `;` or `}` so we can report more than one error per run instead of dying on the first.

### 3.3 Semantic analysis (`src/sema.cpp`)
A tree walk with a **scoped symbol table** (stack of hash maps). Responsibilities:
- Resolve every identifier to a declaration; error on use-before-declare and redeclaration in the same scope.
- Type-check every expression bottom-up; annotate each `Expr` with its resolved type.
- Check calls: function exists, arity matches, argument types match, result type used correctly.
- Enforce `if/while` conditions are `bool`; enforce return type matches; enforce "all paths return".
- Build the function table used by IRGen.

Output: the same AST, now annotated with types, plus a symbol/function table. Or a list of diagnostics.

### 3.4 IR (`include/ir.h`, `src/irgen.cpp`)
**Linear three-address IR**, organized `Module → Function → BasicBlock → Instruction`.

- **Values** are either a **temporary** (`%t3`, assigned exactly once — SSA-like for temps, which makes temp-level optimization trivially sound) or a **constant**. Named variables are *not* SSA; they live in named slots accessed by `load`/`store`. (Full SSA with phi insertion is out of scope for day-1 — see §6.)
- **Basic block**: a straight-line list of instructions ending in exactly one **terminator** (`br`, `br_cond`, `ret`). Blocks form a CFG.

Instruction set (each `%t = ...` defines a temp):
```
%t = const <int|bool>
%t = <binop> %a, %b          ; add sub mul sdiv smod  and or  cmp_lt cmp_le cmp_gt cmp_ge cmp_eq cmp_ne
%t = <unop> %a               ; neg  not
%t = load  <slot>            ; read a named local
      store <slot>, %a       ; write a named local  (side effect)
%t = call  <fn>(%a, %b, ...) ; (side effect: conservatively, calls may print)
      print %a               ; builtin (side effect)
      br    <bb>
      br_cond %a, <bbT>, <bbF>
      ret   %a  |  ret
```

Lowering highlights:
- `if/else` → condition block + `br_cond` to then/else blocks + a merge block.
- `while` → header block (evaluate cond, `br_cond` body/exit) + body block that branches back to header.
- `&&`/`||` → short-circuit branch diamonds producing a bool temp in a merge block (**not** a single `and`/`or` instruction — that would evaluate both sides).

### 3.5 Optimization passes (`src/passes/*.cpp`)
A `Pass` interface + `PassManager` that runs a pipeline and **iterates to a fixpoint** (passes expose "did I change anything?"), because folding creates new constants that unlock more propagation that unlocks more DCE, etc.

```cpp
struct Pass {
    virtual const char* name() const = 0;
    virtual bool run(Function&) = 0;   // returns true if it modified the function
    virtual ~Pass() = default;
};
```

Passes for v1:
1. **Constant folding** — `add(const,const) → const`, comparisons on constants, unary on constant. Per-instruction, obviously sound.
2. **Algebraic simplification** — `x+0→x`, `0+x→x`, `x-0→x`, `x*1→x`, `1*x→x`, `x*0→0`, `x-x→0`, `x/1→x`, `x&&true→x`, `x||false→x`, etc. Careful: `x*0→0` is fine for `int`; document each identity.
3. **Local constant propagation** — within a basic block, track which temps/slots hold known constants; substitute constant operands so folding can fire. Reset knowledge at block boundaries (sound without global dataflow) and on any `store` to a slot / `call` (may clobber). Global version is a stretch goal (§6).
4. **Dead code elimination** —
   a. *Dead temp elimination*: an instruction whose result temp has no uses **and** no side effects is removed (side-effecting = `store`, `print`, `call`, terminators).
   b. *Unreachable block elimination*: blocks not reachable from entry in the CFG are dropped.
   c. *Dead store elimination (local, conservative)*: a `store slot` immediately followed (within the same block, before any `load slot` or `call`) by another `store slot` — the first is dead. Kept intentionally conservative because the sound general version needs liveness analysis (§6, and a great interview talking point).

**The invariant that governs all of this:** every pass must preserve observable behavior — the sequence of `print`s and the return value. Our optimization tests assert *both* "instruction count went down" *and* "program output is byte-identical to `-O0`". That pairing is the whole correctness story.

### 3.6 Backend — bytecode + VM (`src/codegen.cpp`, `src/vm.cpp`)
Lower optimized IR to a **register-based bytecode** (registers map ~1:1 to IR temps + local slots), then execute on a small VM with a value stack of call frames. Register (not stack) VM is a deliberate choice: lowering is almost a direct serialization of the three-address IR, so codegen stays simple and debuggable — and "register vs stack VM tradeoffs" is a clean interview conversation (Lua 5 / Dalvik are register VMs; JVM / CPython are stack VMs).

Bytecode ops mirror the IR: `CONST, MOV, ADD…, CMP_*, NEG, NOT, LOAD, STORE, JMP, JMP_IF_FALSE, CALL, RET, PRINT, HALT`. The VM does division-by-zero trapping and a bounded call stack (stack-overflow trap).

A second **C-source backend** (emit portable C, compile with the system `cc`) is a stretch goal that substantiates "new code-generation passes could be added in isolation" and gives a genuinely native execution path.

---

## 4. CLI / driver (`src/main.cpp`)

```
dslc <file.dsl> [options]
  --emit=<tokens|ast|ir|ir-opt|bc>   dump an intermediate stage and stop
  -O0 | -O1                          disable / enable the optimization pipeline (default -O1)
  --run                              execute on the VM (default action if no --emit)
  --stats                            print instruction counts before/after optimization
```

This makes each pipeline arrow directly observable — the debugging surface and the demo surface are the same thing.

---

## 5. Testing strategy (`tests/`)

Lightweight header-only harness (`tests/test.h`: `CHECK`, `CHECK_EQ`, `RUN_SUITE`) so there's **no external test dependency** to fetch — every suite is its own executable registered with `add_test`, run via `ctest`.

Layers:
1. **Lexer tests** — token kinds/lexemes/positions; tricky cases (`->` vs `-`, `==` vs `=`, comments, EOF).
2. **Parser tests** — precedence/associativity produce the right tree shape; error + recovery cases.
3. **Sema tests** — every diagnostic fires when it should and *doesn't* when it shouldn't (type errors, arity, undeclared, non-bool condition, missing return).
4. **IR tests** — golden IR text for small programs; short-circuit lowering shape; CFG well-formedness (every block ends in exactly one terminator).
5. **Optimization tests** — the key ones: for a corpus of programs, assert `instr_count(-O1) < instr_count(-O0)` **and** `output(-O1) == output(-O0)`. This is the resume claim, mechanically verified.
6. **End-to-end tests** — `examples/*.dsl` each with an expected-output golden file; source → compile → run → diff.

`ctest` is the single entry point; CI-friendly.

---

## 6. Known holes / risks / deliberate scope cuts

Being explicit here because these are exactly what an interviewer probes.

- **No full SSA / phi nodes.** Named vars stay in memory (`load`/`store`); optimization on them is *local* (per-block): the const/copy-propagation pass tracks each slot's current value within a block and eliminates redundant loads (which does shrink loop bodies — see `gcd`), but it resets at every block boundary and never propagates a value across a branch merge or back-edge. *Sound but not maximal.* Global constant propagation (a CFG worklist / sparse conditional constant propagation) is the natural next step and is scoped as a stretch goal. Calling this out beats pretending the optimizer is global.
- **Conservative dead-store elimination.** The general case needs backward liveness analysis; we ship the safe local case and name the limitation. Getting DSE *wrong* silently corrupts programs — so conservative-but-correct is the right day-1 call.
- **Integer overflow wraps** (two's complement) and is *not* trapped. Note this is implemented deliberately, not inherited: signed overflow is **undefined behaviour** in C++, so both the constant folder and the VM perform arithmetic in unsigned (where wraparound is defined) and convert back, with explicit guards for `INT64_MIN / -1` and `INT64_MIN % -1`. Both sides use identical helpers so a folded constant and a runtime computation can never disagree — otherwise `-O0` and `-O1` could differ. (UBSan caught the original UB here; see `docs/CHALLENGES.md` CHAL-008.)
- **No arrays, strings, floats, or user structs.** Keeps types trivial; the language is still Turing-complete (loops + ints), so optimizations and codegen are exercised on real algorithms.
- **`call` is treated as opaque/side-effecting** for optimization (may print, may not terminate). No interprocedural analysis, no inlining. Inlining is an obvious extension and a good talking point.
- **Register bytecode VM, not native codegen.** Chosen for testability and time; the C backend stretch goal is the bridge to "real" native code.
- **Fixpoint termination:** the pass pipeline must strictly reduce a well-founded measure (instruction count / constant-ness) each iteration, or cap iterations. We cap iterations *and* rely on monotonic simplification to guarantee it halts — worth stating so we don't hand-wave an infinite loop.

---

## 7. Task breakdown (build order)

| # | Task | Deliverable | Tests |
|---|------|-------------|-------|
| 0 | Skeleton | CMake, dirs, `test.h`, `dslc` stub, first green `ctest` | smoke |
| 1 | Lexer | `token.h`, `lexer.{h,cpp}` | lexer suite |
| 2 | Parser | `ast.h`, `parser.{h,cpp}`, `--emit=ast` | parser suite |
| 3 | Sema | `sema.{h,cpp}`, diagnostics | sema suite |
| 4 | IR + IRGen | `ir.h`, `irgen.{h,cpp}`, IR printer, `--emit=ir` | IR/golden suite |
| 5 | Passes I | pass framework, constfold, algebraic | pass suite |
| 6 | Passes II | constprop, DCE, `--stats` | opt suite (size↓ + output==) |
| 7 | Backend | bytecode, codegen, VM, `--run` | e2e suite |
| 8 | Driver + polish | full CLI, examples, README, size report | e2e/examples |
| 9 | Stretch | C backend / CSE / global constprop | extra |

Each task: implement → explain the interesting **debugging decision** and the **interview challenge** it maps to → test → move on.
