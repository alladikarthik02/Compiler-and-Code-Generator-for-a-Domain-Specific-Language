# `dslc` — Full Revision Guide

Everything from the walkthrough, in one place. Read a module, then say the interview answers **out loud**. The goal is not to memorize lines — it's to understand ~6 ideas and how data flows between them, so you can re-derive any line.

## The whole pipeline (memorize this diagram)

```
source text
  → Lexer      (Mod 1)  characters → tokens
  → Parser     (Mod 2)  tokens → AST (a tree)
  → Sema       (Mod 3)  is the tree meaningful? types, scopes, returns
  → IR + IRGen (Mod 4)  tree → flat three-address instructions (blocks + CFG)
  → Optimizer  (Mod 5)  fewer instructions, identical behavior
  → Backend    (Mod 6)  IR → bytecode → VM executes → output
```

Each stage hands a cleaner representation to the next. You can inspect every arrow from the CLI:
`--emit=tokens | ast | ir | ir-opt | bc`, plus `-O0`/`-O1` and `--stats`.

---

# MODULE 1 — LEXER

**Idea:** turn the raw character string into a flat list of **tokens** (smallest meaningful units): keywords, identifiers, numbers, operators. It only splits & labels — it does NOT check if the program makes sense.

**Design decisions:**
- Hand-written (not `flex`) — clearer, debuggable, better errors.
- Track **line/column from character one** — every later error message depends on it; can't recover positions later.
- **Maximal munch** for multi-char operators — grab the longest valid token.

**Key mechanism — maximal munch:** on `-`, peek at the next char; if `>`, make `->`, else `-`. Same for `==`/`=`, `<=`/`<`, `!=`/`!`. The helper `match(x)` = "if next char is x, consume it and return true, else leave it."

**Two SEPARATE mechanisms (don't mix these up):**
- **Keyword lookup** = ONLY for letter-words. Scan a whole word (`let`), look it up in a table → keyword or identifier.
- **Maximal munch** = for symbols/operators. Peek one char ahead. **No table, no keywords.**

**Tested:** token kinds, `->` vs `-`, integers→values, `let` vs `letx`, comment skipping + positions, error on `@`.

**Interview Q&A:**
- *What does a lexer do?* Scans characters L→R into tokens tagged with kind + position. No validation.
- *`->` vs `-`?* Maximal munch — consume `-`, peek for `>`.
- *Keyword vs variable?* Scan the word, look it up in a keyword table.
- *Invalid input `@`?* Never crashes — emits an Error token + diagnostic, keeps scanning.
- *Why an EOF token?* So the parser always has a definite "end" to read; no bounds checks.

---

# MODULE 2 — PARSER

**Idea:** turn the flat token list into a **tree (AST)** that captures structure and operator precedence. `1 + 2 * 3` → tree with `*` deeper than `+`.

**Design decisions:**
- **Recursive descent** for statements — one function per grammar rule; the call stack mirrors the code's nesting.
- **Pratt parsing** (precedence climbing) for expressions — one function + a precedence table, instead of a ladder of functions per level.
- **Panic-mode error recovery** — report one error, `synchronize()` to the next `;`/`}`, resume. One error per real mistake.

**The AST:** two families — **Expressions** (produce a value: IntLit, BoolLit, VarExpr, UnaryExpr, BinaryExpr, CallExpr) and **Statements** (do something: Let, Assign, If, While, Return, Print, ExprStmt, Block). Children held by `std::unique_ptr` (exclusive ownership, auto cleanup, no leaks). Operators use their own `BinOp` enum so the AST doesn't depend on token kinds.

**The Pratt engine (`parse_binary`):**
```
lhs = parse_unary()
while next token is an operator with precedence >= min_prec:
    consume operator
    rhs = parse_binary(operator_precedence + 1)   // the +1 is the whole trick
    lhs = BinaryExpr(op, lhs, rhs)
return lhs
```

**Precedence vs associativity (don't confuse them):**
- **Precedence** = between DIFFERENT-strength operators (`*` before `+`). Shapes `2 * 3 + 4`.
- **Associativity** = between EQUAL-strength operators (`10 - 2 - 3`). Handled by the `+1`.

**★ Memorable anchor — the `+1` (`10 - 2 - 3`):**
- With `+1` (left-assoc, correct): `(10 - 2) - 3` = **5**.
- Without `+1` (right-assoc, wrong): `10 - (2 - 3)` = **11**.
The `+1` makes an equal-precedence operator on the right get **rejected**, so it nests on the **left**.

**Worked trace `2 * 3 + 4`** (`*`=6, `+`=5): `*` recurses right with min_prec 7 → the `+` (5) is too weak, rejected → `*` closes over `2,3` → outer loop takes `+` with `(* 2 3)` as its left → `(+ (* 2 3) 4)`.

**Tested:** precedence/associativity tree shapes, calls, full program structure, two-errors-both-reported (recovery).

**Interview Q&A:**
- *Recursive descent?* One function per grammar rule; recursion mirrors nesting.
- *Pratt & why?* One function + binding-power table instead of a function per precedence level.
- *Left-associativity?* Recurse with `min_prec + 1` → equal-precedence operator nests on the left. `10-2-3` = `(10-2)-3` = 5, not 11.
- *Parentheses?* Handled in `parse_primary` as a leaf — recursively parse a full expression, expect `)`. Becomes its own subtree → overrides precedence.
- *`x = 5` vs `x + 1`?* One token of lookahead: identifier followed by `=` → assignment.

---

# MODULE 3 — SEMANTIC ANALYSIS (Sema)

**Idea:** the tree is grammatically correct, but is it **meaningful**? Check the rules grammar can't: variables declared before use, types match, correct call arity/types, `if`/`while` conditions are bool, every path returns. Output: same tree annotated with types, or a list of errors.

**Design decisions:**
- **Two passes** — pass 1 collects all function signatures; pass 2 checks bodies. Lets functions call each other in any order.
- **Scoped symbol table** — a stack of `name→type` maps. Push on block entry, pop on exit. Lookup searches innermost→outermost → gives scoping + shadowing for free.
- **Poison type (`Type::Error`)** — after a type error, mark the expression `Error` and stop reporting on things built from it. One error, no cascade.
- **Structural "all-paths-return"** — conservative but sound.

**Type rules:** arithmetic `+ - * / %` : int,int→int. Comparison `< <= > >=` : int,int→**bool**. Equality `== !=` : same type→bool. Logical `&& ||` : bool,bool→bool.

**All-paths-return:** a `return` returns; an `if` returns ONLY if it has an `else` AND both branches return; a `while` NEVER guarantees a return (may run zero times). Conservative: may reject a program that technically always returns, but never accepts one that can fall off the end — the safe direction.

**Tested:** valid programs = 0 errors; each type error fires; scoping/shadowing/redeclare; call arity + arg types; return paths; main signature.

**Interview Q&A:**
- *Sema vs parser?* Parser checks grammar (well-formed tree); Sema checks meaning (types, scopes, arity, returns) and annotates types.
- *Scopes & shadowing?* Stack of symbol tables; push/pop per block; lookup inner→outer.
- *Why two passes?* Collect signatures first so functions can reference each other in any order.
- *Poison type?* Stops one error cascading into many.
- *All-paths-return without running it?* Structural check: return→yes; if→both branches + else; while→never guaranteed.
- *Why `<` takes int but returns bool?* Operands are compared (int), result is a truth value (bool). That's why `if (a < b)` works but `if (a + b)` doesn't.

---

# MODULE 4 — IR (Intermediate Representation) ★ the heart

**Idea:** flatten the tree into a list of dead-simple instructions ("three-address code"), because machines run flat instructions, not trees — AND optimizations are far easier on a flat list. `x = 2 + 3*4` → `t0 = 3*4; t1 = 2+t0; store x, t1`.

**Two kinds of value (the key idea):**
- **Temps (`%0`, `%1`)** = intermediate results, **assigned exactly once** (SSA-like). Because a temp never changes, `%0 = 7` means `%0` is 7 *everywhere* → trivially safe to optimize.
- **Slots (`@0`, `@1`)** = named variables in memory, **can be reassigned** → accessed via `load`/`store`.

**Design decision (say this):** temps are SSA, variables are memory slots. This avoids **phi nodes** (the SSA merge-point instructions — `x = φ(1 from then, 2 from else)` — that are complex to insert) at the cost of variable optimization being **block-local**.

**Basic block & CFG:** a basic block = a straight-line run of instructions with one entry, one exit, ending in exactly one **terminator** (`br`, `condbr`, `ret`). Blocks connect via terminators into a **Control-Flow Graph**. An `if` = a diamond; a `while` = a block that jumps back to its header (the back-edge).

**Lowering:**
- `if` → `condbr` into `then`/`else` blocks that both `br` to a `merge` block.
- `while` → `header` (tests condition) `condbr`→ `body`/`exit`; body ends with `br header` (the loop back-edge). Condition re-evaluated in the header each iteration.
- `finalize_terminators` → any block missing a terminator gets an implicit `ret 0` (keeps the IR well-formed).

**★ Short-circuit `&&`/`||` (the money topic):** can't be one `and` instruction because `&&` must NOT evaluate the RHS if the LHS is false. Lowered to a **branch diamond**: eval LHS → `condbr`; the "short" block stores the short-circuit constant (0 for `&&`) WITHOUT touching the RHS; the "rhs" block evaluates the RHS; both write a scratch slot the merge block loads. **The short-circuit is encoded in the control-flow shape, not an instruction.**

**Two helper predicates DCE relies on:** `op_is_terminator` (br/condbr/ret) and `op_has_side_effect` (store/print/call/terminators — these can't be deleted even if unused, because they DO something observable).

**Tested:** CFG well-formedness (every block ends in exactly one terminator), `2+3*4` flattens to one mul + one add, short-circuit emits `condbr` and NO `and`.

**Interview Q&A:**
- *What is an IR & why?* A flatter, simpler form between AST and machine; three-address code; it's what optimizations run on.
- *Three-address code?* Instructions with ≤3 operands (2 in, 1 out); nested expressions broken into a sequence of these.
- *Basic block / CFG?* Straight-line code, single entry/exit, ends in one terminator; blocks form a graph via their terminators.
- *Temps vs variables & why?* Temps SSA (safe to reason about globally); variables in memory slots. Chosen to avoid phi-node insertion; trade-off = variable optimization is block-local.
- *Lower an `if`/`while`?* if→condbr to then/else→merge. while→header tests condition, body jumps back to header (back-edge).
- *★ Compile `a && b`, why not one instruction?* Must short-circuit: only evaluate `b` if `a` is true. Lower to a branch — the RHS lives in its own block that's skipped when the LHS decides the result. Matters when RHS divides by zero or has side effects.
- *Why keep instructions whose result is unused?* Side effects — store/print/call/terminators are observable.

---

# MODULE 5 — OPTIMIZER

**Idea:** rewrite the IR to be smaller/faster while producing **identical output**.

**★ The governing invariant:** every optimization must (1) shrink the code AND (2) keep output byte-identical to `-O0`. If output changes, it's a bug. Both halves are checked in tests.

**The framework:** small independent **passes**, each `run(f) → bool changed`. A `PassManager` runs them **to a fixpoint** — loops the whole pipeline until a full round changes nothing. Default pipeline: `constfold → algebraic → constprop → dce`.

**Why loop?** Passes unlock each other: fold `3*4→12` → propagation replaces `12` everywhere → the defining instruction becomes unused → DCE removes it → may unlock more folding.

**Why it terminates:** every pass only simplifies (fold/forward/delete), so a measure (instruction count) strictly decreases until it can't. Plus a hard cap `max_iters = 50` (real programs converge in 2–4 rounds; the cap is a backstop so a bug can't hang the compiler).

**The four passes:**
1. **Constant folding** — all-constant operations computed at compile time (`mul 3,4 → const 12`). **Refuses to fold `x/0` or `x%0`** — left to trap at runtime.
2. **Algebraic simplification** — `x+0→x`, `x*1→x`, `x*0→0`, `x-x→0`, `x/1→x`, `x%1→0`. **`x*0→0` is exact for ints but WRONG for floats** (`NaN*0=NaN`).
3. **Const/copy propagation + redundant-load elimination** — forward temp values **globally** (safe: SSA); track variable/slot values **within a block only** and reset at boundaries. Made the gcd loop shrink 17% by removing repeated loads.
4. **Dead-code elimination** — unreachable blocks (reachable-from-entry, renumber & patch targets); dead temps (unused result + no side effect); **conservative** dead stores (store to a never-loaded slot, or immediately overwritten with no read between). Conservative because the general case needs backward **liveness analysis**, and wrong DSE silently corrupts data.

**★ Soundness boundary:** temps propagate globally (SSA → one value everywhere); variables only block-locally (they can change; cross-block reasoning needs CFG dataflow or SSA/phi). This is why loops aren't miscompiled.

**Tested:** reductions happen (`2+3*4`→`14`, `x*0`→`0`, dead vars gone, nested constants fold, gcd loop shrinks) AND **semantic preservation** (`-O0` output == `-O1` output over a corpus).

**Interview Q&A:**
- *Which optimizations?* Constant folding, algebraic simplification, const/copy propagation + redundant-load elimination, DCE (unreachable/dead-temp/dead-store).
- *The one rule?* Never change observable behavior — verified by fewer-instructions AND identical output.
- *Why loop to a fixpoint?* Passes enable each other; loop until nothing changes; terminates because passes only simplify.
- *Sound across a loop?* Temps global (SSA); slots block-local (reset at boundaries) → no cross-block claims.
- *Safe to delete?* Only pure, unused-result instructions; keep stores/prints/calls/terminators.
- *Why DSE conservative?* General case needs liveness; wrong DSE corrupts data silently.
- *Why not fold `10/0`?* It's a defined runtime trap; folding would crash or invent a value.

---

# MODULE 6 — BACKEND (codegen + VM)

**Idea:** "lower" the optimized IR to a flat **register bytecode**, then a small **VM** executes it to produce output. This is the "lower to executable code" step.

**Design decisions:**
- **Register bytecode (not stack)** — the IR is already three-address, so lowering is nearly 1:1 (register = temp). Register VMs: Lua 5, Dalvik; stack VMs: JVM, CPython.
- **A VM (not native codegen / LLVM)** — portable, testable, debuggable.
- **Tagged operands** — an operand is an immediate constant OR a register, so `print 14` carries `14` inline (no wasted load).

**IR block-ids → flat jump indices (backpatching — the star mechanism):** IR jumps target block ids; bytecode jumps target absolute positions in the flat array.
1. Lay blocks out in order, recording `block_start[id] = current index`.
2. Emit instructions (jumps still hold block ids — because forward jumps target not-yet-emitted blocks).
3. Second pass: rewrite every jump's target: `target = block_start[target]`.
`condbr` becomes TWO instructions: `jmpf cond -> false_target` then `jmp -> true_target`.

**★ Distinction to nail:** block **ids are known** all along; a block's **flat index is unknown** until it's laid out, and forward jumps target later blocks → so ids are placeholders, patched once layout is done.

**The VM (`call`):** each call gets its own **frame** — fresh `regs` (registers) and `slots` (variables) vectors; arguments go into the first slots. Then a **fetch-decode-execute** loop: a program counter `pc` indexes the instruction array, a `switch` on the opcode does the operation, `pc` advances or jumps.

**Function calls = recursion:** `Call` recursively invokes `call(callee, args)`; each invocation gets its OWN regs/slots → locals never clash between calls.

**★ Where frames live & why the cap:** frames live **on the host C++ call stack** — `VM::call` is a recursive C++ function, and `regs`/`slots` are its local variables. So DSL recursion depth = C++ stack depth. A real stack overflow is an **uncatchable segfault**, so we count `depth_` and throw a `Trap` at `kMaxDepth = 5000` — turning an uncatchable crash into a clean, catchable runtime error.

**Traps:** div/mod by zero and stack overflow throw a `Trap`, caught at the top → `VMResult{ok=false, error=...}`. Graceful failure, not undefined behavior.

**★ Frame corruption anchor (`fib`):** if all calls shared one `regs` array, when `fib(3)` calls `fib(2)`, the callee overwrites `r5` (which held `fib(n-1)` and had to stay live across the call to `fib(n-2)`) → the final `add r5, r8` uses garbage → wrong result. Fresh per-call frames prevent it. On a real CPU (one physical register file) this is solved by **calling conventions** (caller-/callee-saved registers, spills).

**Tested (end-to-end):** known outputs (fib=55, fact=120, gcd=12, sum=5050); semantic preservation; runtime trap on `10/0`; short-circuit avoids the trap.

**Interview Q&A:**
- *What does the backend do?* Lowers optimized IR to register bytecode; a VM executes it.
- *Register or stack VM & why?* Register — IR is three-address, so mapping is ~1:1.
- *IR branches → runnable jumps?* Record each block's flat start, then backpatch every jump's block-id to an absolute index.
- *How does the VM run?* Fetch-decode-execute: pc indexes code, switch on opcode, advance/jump; regs+slots per frame.
- *Function calls?* Recursive interpreter call with a fresh frame; args into first slots; host stack holds frames; depth counter traps.
- *Div by zero?* Throws a trap caught at the top → clean error, not a crash.

---

# RECURRING ENGINEERING PRINCIPLES (point to these — they impress)

- **Bound the unbounded, fail cleanly.** `max_iters = 50` (optimizer), `kMaxDepth = 5000` (VM), lexer never crashes on bad input. Every unbounded thing has a guard that turns a hang/crash into a clean error.
- **Separation of concerns.** Each stage does exactly one job and hands a cleaner representation onward. The lexer doesn't validate; the parser doesn't type-check; the optimizer doesn't change behavior.
- **Correct-but-conservative beats aggressive-but-wrong.** DSE, cross-block propagation, all-paths-return — all deliberately conservative because silent miscompiles are the worst outcome.
- **Prove it, don't claim it.** The optimizer's safety isn't asserted — it's verified on every build by the `-O0 == -O1` output-equality test.

# QUICK REFERENCE

**Build & test:**
```sh
cmake -B build -S . && cmake --build build
ctest --test-dir build --output-on-failure
```
**Run / inspect:**
```sh
./build/dslc examples/fib.dsl                    # compile @ -O1 and run
./build/dslc file.dsl --stats                    # instruction counts before/after
./build/dslc file.dsl --emit=tokens|ast|ir|ir-opt|bc
./build/dslc file.dsl -O0                         # disable optimizer
```
**File map:** `token.h`/`lexer.*` (Mod 1) · `ast.h`/`parser.*` (Mod 2) · `sema.*` (Mod 3) · `ir.h`/`irgen.*` (Mod 4) · `pass.h`/`passes/*` (Mod 5) · `bytecode.h`/`codegen.*`/`vm.*` (Mod 6). Tests: `tests/test_*.cpp`.

**Language:** statically typed; types `int` (64-bit) and `bool`; functions, `if/else`, `while`, `let`/assign, `print`, recursion, short-circuit `&&`/`||`.
