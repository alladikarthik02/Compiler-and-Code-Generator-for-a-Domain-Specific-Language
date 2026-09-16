# `dslc` — Challenge Log (real bugs from this build)

These are **only the actual engineering problems we hit while building this compiler** — the commands that failed, the errors we saw, and the fixes we applied — reconstructed from the build session and written in plain English. Nothing here is a "typical problem" added for completeness; every entry really happened. Terms of art are defined the first time they appear.

---

## CHAL-001 — Couldn't pull the text out of the résumé PDF

- Date/phase: Session start — reading the résumé to find which project to build.
- Status: **Resolved**

### What we were doing
The very first step was to read the résumé PDF so we could pick the "second project" to build. I tried to open it with the built-in file reader.

### Background
A PDF can hold text two ways: as a **text layer** (the characters are stored as real text you can copy) or as a **scanned image** (a picture of text, which needs OCR — optical character recognition — to read). Rendering PDF pages to images on this machine relies on a tool called **poppler** (its command is `pdftoppm`).

### What went wrong
The first read returned no usable text. Asking for specific pages failed with:
```
pdftoppm is not installed. Install poppler-utils (e.g. `brew install poppler`).
```

### Why
The image-rendering path needed `poppler`, which isn't installed on this Mac. That path was a dead end for a machine without the dependency.

### Fix
Instead of rendering images, I went straight to the PDF's **text layer** with a small Python script using the `pypdf` library (installed on the spot). The résumé's text came out cleanly, and we found the target project (the DSL compiler).

### Takeaway
When one extraction tool is missing a dependency, don't fight it — reach for the text layer directly with a library instead of the image/OCR path.

---

## CHAL-002 — The compiler couldn't find `<string>` (the standard library looked "missing")

- Date/phase: Task 0 — first build of the empty project skeleton.
- Status: **Resolved** (this was the big one)

### What we were doing
Compiling the very first file, which just did `#include <string>` — pulling in C++'s standard text type. This should be the most boring line in the whole project.

### Background
- A **header** (`<string>`) is a file the compiler pastes in so your code knows what `std::string` is.
- The compiler finds headers by searching a list of **include directories** in order (its "search path"), grabbing the first match.
- On a Mac, C++'s standard library headers ship inside the **SDK** (Software Development Kit — Apple's bundle of system headers) and/or the **Command Line Tools** (the smaller developer toolset). `-isysroot` points the compiler at an SDK; `-isystem DIR` adds a directory to the search path; `-nostdinc++` says "ignore your default C++ header locations and use only the ones I give you."

### What went wrong
```
fatal error: 'string' file not found
```
And it still failed even after pointing the compiler at the SDK with `-isysroot`.

### Why
There were **two copies** of the C++ standard library headers on the machine, and the compiler was grabbing the wrong one. The copy the compiler preferred — inside the Command Line Tools (`.../CommandLineTools/usr/include/c++/v1`) — was a **broken stub with only 3 files** and no `<string>`. The real, complete copy (**183 files**) lived in the SDK. Analogy: two books with the same title on the shelf; the librarian always handed over the empty one.

<details><summary>How we proved it</summary>

```
CLT   .../CommandLineTools/usr/include/c++/v1  →   3 files   (broken, no <string>)
SDK   .../MacOSX.sdk/usr/include/c++/v1        → 183 files   (the real library)
```
Forcing the SDK copy compiled the test file cleanly.
</details>

### Fix
In `CMakeLists.txt`, detect this situation and force the compiler to use the SDK's real library:
```
-nostdinc++ -isystem <sdk>/usr/include/c++/v1
```
`-nostdinc++` throws away the default (broken) location; `-isystem <sdk>/.../v1` supplies the good one. Build went green.

### Takeaway
"Header not found" usually means the compiler is **looking in the wrong place**, not that the header is missing — compare the compiler's actual include search order against where the file really lives.

---

## CHAL-003 — Compiler warning: an unused helper in the parser tests

- Date/phase: Task 2 — building the parser test suite.
- Status: **Resolved**

### What we were doing
Compiling `test_parser.cpp` after writing the parser tests.

### Background
A **compiler warning** isn't an error — the program still builds — but it flags something suspicious. `-Wunused-function` warns about a function that's defined but never called (usually leftover code).

### What went wrong
```
warning: unused function 'parse' [-Wunused-function]
```

### Why
I'd written a `parse` helper and then ended up not using it — dead code.

### Fix
Deleted the unused helper. Warning gone.

### Takeaway
Treat warnings as signal, not noise — remove dead code rather than letting it accumulate.

---

## CHAL-004 — Compiler warning: two strings that looked like a missing comma

- Date/phase: Task 4 — building the IR test suite.
- Status: **Resolved**

### What we were doing
Compiling `test_ir.cpp`, which contained a two-line test program stored as a string.

### Background
In C and C++, two string literals written next to each other are **automatically glued together**: `"ab" "cd"` becomes `"abcd"`. Handy for splitting a long string across lines. But inside a list of strings, that same pattern looks exactly like you *forgot a comma* between two list items — so the compiler warns.

### What went wrong
```
warning: suspicious concatenation of string literals ... did you mean to separate the elements with a comma? [-Wstring-concatenation]
```

### Why
I deliberately split one test program across two adjacent string literals (relying on the auto-join). But because they sat inside an array of programs, the compiler couldn't tell "intentional join" from "forgotten comma."

### Fix
Wrapped the two literals in parentheses — `("...\n" "...")` — which tells the compiler "yes, I meant to join these."

### Takeaway
Adjacent string literals auto-join in C/C++; parenthesize them inside a list so the code doesn't read as a missing comma.

---

## CHAL-005 — Name clash: two different things both called `Program`

- Date/phase: Task 7 — wiring up the backend (bytecode + VM).
- Status: **Resolved**

### What we were doing
Adding the backend and building the whole thing together for the first time.

### Background
A **type name** is a label for a kind of data. If two different types share the exact same name and both are visible in the same file, the compiler can't tell which one you mean — a **name collision**.

### What went wrong
```
error: redefinition of 'Program'
```

### Why
The parser layer already had a type called `Program` (the whole parsed program — the AST). The new backend layer defined *another* `Program` (the compiled bytecode program). The main driver file included **both** headers, so the compiler saw two different definitions of the same name and refused to continue.

### Fix
Renamed the backend's type to `BCProgram` ("bytecode program") everywhere it appeared, leaving the AST's `Program` alone.

### Takeaway
In a layered system, give each layer's types distinct names — a generic name like `Program` will eventually collide with another layer's.

---

## CHAL-006 — The rename command silently did nothing (BSD vs GNU `sed`)

- Date/phase: Task 7 — while performing the `Program` → `BCProgram` rename from CHAL-005.
- Status: **Resolved**

### What we were doing
Renaming `Program` to `BCProgram` across several files with a stream-editing command (`sed`).

### Background
`sed` is a command-line tool that find-and-replaces text in files. In a search pattern, `\b` means "a word boundary" (so it matches whole words only). Crucially, **there are two different `sed`s**: **GNU sed** (on Linux) understands `\b`; **BSD sed** (the one that ships with macOS) does **not**.

### What went wrong
The command `sed 's/\bProgram\b/BCProgram/g' ...` ran with no error — but changed nothing. A follow-up check found zero occurrences of `BCProgram`, meaning nothing was actually replaced.

### Why
macOS's BSD `sed` doesn't support `\b`. So the pattern `\bProgram\b` matched *nothing*, and `sed` "succeeded" at replacing nothing — a silent no-op, the worst kind because there's no error to alert you.

### Fix
Dropped `\b` and used a plain `sed 's/Program/BCProgram/g'`, after first checking those files contained no *other* `Program` that the broader pattern would wrongly hit.

### Takeaway
macOS's BSD `sed` is not GNU `sed` — GNU extensions like `\b` fail silently; always verify a rename actually changed the files instead of assuming success.

---

## CHAL-007 — A test failed because the optimizer got *better*

- Date/phase: Task 8 — strengthening the optimizer with redundant-load elimination.
- Status: **Resolved**

### What we were doing
Improving the optimizer so it could also remove redundant memory reads inside loops (e.g. loading the same variable twice with nothing changed between). After the improvement, we reran the test suite.

### Background
A **regression test** pins down expected behavior so that future changes don't accidentally break it. But a test can also pin down behavior that was *deliberately* meant to change — and then it fails on purpose-built improvements. The instruction count here is measured on the compiler's internal instruction list (its IR).

### What went wrong
```
FAIL test_opt.cpp: CHECK_EQ(before, after)  [18 != 15]
```

### Why
An older test asserted that the `gcd` program's instruction count was **unchanged** by optimization (18 → 18) — that was true back when the optimizer left loops alone. After adding redundant-load elimination, the optimizer legitimately shrank `gcd` from 18 to 15 instructions. So the test failed — but the **code was correct**; the test's *expectation* was now out of date. (The separate end-to-end test confirmed the program still produced identical output, i.e. the optimization was safe.)

### Fix
Repurposed the test: instead of "optimization changes nothing here," it now asserts "optimization *does* shrink this loop" (`after < before`), and correctness (same output) is guarded by the end-to-end semantic-preservation test.

### Takeaway
When you improve a system, tests that froze the old behavior will fail — learn to tell a real regression ("I broke something") from a stale expectation ("the test assumed the old, weaker behavior").

---

---

## CHAL-008 — Sanitizers found undefined behaviour hiding behind passing tests

- Date/phase: Post-completion hardening — running the test suite under ASan/UBSan.
- Status: **Resolved**

### What we were doing
All seven test suites passed, so the compiler looked correct. To check for problems the tests couldn't see, we rebuilt everything with two runtime checkers switched on and re-ran the suite plus every example program.

### Background
- **AddressSanitizer (ASan)** and **UndefinedBehaviorSanitizer (UBSan)** are compiler options (`-fsanitize=address,undefined`) that add checking code into your program. ASan catches memory mistakes; UBSan catches **undefined behaviour** — operations the C++ standard gives no meaning to, where the compiler is free to do *anything*.
- **Signed integer overflow** — making an `int` bigger than its maximum — is one of those. Many people assume it "wraps around" to the negative end. **In C++ it does not: it is undefined behaviour.** (Unsigned types *are* defined to wrap; signed types are not.)

### What went wrong
Two findings:

1. Compiling a program containing `9223372036854775807 + 1` produced:
```
src/passes/constfold.cpp:10:31: runtime error: signed integer overflow:
9223372036854775807 + 1 cannot be represented in type 'int64_t'
```
2. Deeply recursive programs crashed the sanitizer build (`AddressSanitizer:DEADLYSIGNAL`) at a recursion depth the normal build handled fine.

### Why
1. The constant folder computed `out = a + b` directly on `int64_t`. When the program's own constants overflowed, **the compiler itself performed a signed overflow** — undefined behaviour. The same bug existed in the VM's arithmetic, and in `-x`, `INT64_MIN / -1` and `INT64_MIN % -1` (all of which overflow). Worse, `docs/SPEC.md` claimed overflow "follows two's-complement wraparound" — which was simply **wrong**; C++ promises no such thing.
   The subtle danger: if the folder and the VM ever disagreed about an overflowing result, a program would print one answer at `-O0` and a different one at `-O1` — silently breaking the project's core guarantee.
2. Not a product bug. Sanitizer instrumentation makes every stack frame much larger, so the same number of recursive calls overflows the real stack in that build only. The normal build handles depth 4900 and traps cleanly at the `kMaxDepth = 5000` guard.

### Fix
All folding and VM arithmetic now goes through shared helpers that do the maths in **unsigned** (where wraparound *is* defined by the standard) and convert back:
```cpp
inline int64_t wrap_add(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
```
plus explicit guards for `INT64_MIN / -1` and `INT64_MIN % -1`. This delivers exactly the two's-complement behaviour the spec intended, without UB. The helpers are duplicated in `constfold.cpp` and `vm.cpp` **on purpose, with comments in both**, precisely so compile-time and runtime results can never drift apart. A regression test in `tests/test_e2e.cpp` asserts the wrapped values and that `-O0` and `-O1` agree. `docs/SPEC.md` was corrected. Finding 2 is documented as a build-configuration limitation, not fixed.

### Takeaway
Passing tests prove your code does what you tested; sanitizers prove it does not do things you never thought to test — and in a compiler, undefined behaviour in the *optimizer* is the most dangerous kind, because it can make optimized and unoptimized builds silently disagree.

---

*All eight challenges above are resolved. The project builds clean (zero warnings), all seven test suites pass, and the suite runs clean under AddressSanitizer and UndefinedBehaviorSanitizer.*
