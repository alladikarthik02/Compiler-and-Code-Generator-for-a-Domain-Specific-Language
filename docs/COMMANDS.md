# `dslc` — Commands Reference

Every command needed to build, test, run, and inspect this project — plus the diagnostic commands we used while building it. For each command: **what it does**, **every argument explained**, and **how to run it**. Written for someone who hasn't used CMake/CTest before.

> All commands assume you are inside the project folder first:
> ```sh
> cd ~/Downloads/dslc
> ```

---

## 0. Quick start (the three commands you need most)

```sh
cmake -B build -S .                            # 1. configure (once)
cmake --build build                            # 2. compile
ctest --test-dir build --output-on-failure     # 3. run all tests
./build/dslc examples/fib.dsl                  # 4. compile + run a program
```
If you only remember four commands, remember these. Everything below is detail and variations.

> ⚠️ **Common typo:** the configure command is `cmake -B build -S .` — note the **`.`** at the end (it's the value for `-S`, the *source directory*). Writing `cmake -B build -S` with no `.` gives `CMake Error: No source directory specified for -S`, and the next command then fails with `build is not a directory` because `build/` was never created. The fix is just adding the `.`.

---

## 0b. Even quicker: shell shortcuts (set up in `~/.zshrc`)

The project's build/test/run commands are wrapped as shell shortcuts in `~/.zshrc`, so you don't have to remember any CMake flags. They use absolute paths, so they work **from any folder**.

| Shortcut | Runs | What it does |
|----------|------|--------------|
| `dslc-build` | `cmake -B … -S … && cmake --build …` | configure + compile |
| `dslc-test` | `ctest --test-dir … --output-on-failure` | run all 7 test suites |
| `dslc-clean` | `rm -rf …/build` | delete the build folder (free ~28 MB) |
| `dslc-rebuild` | `dslc-clean && dslc-build` | clean build from scratch |
| `dslc <file.dsl>` | `…/build/dslc <file.dsl>` | run the compiler from anywhere (the build dir is on `PATH`) |

**Typical flow:**
```sh
dslc-build                        # after editing code
dslc-test                         # confirm everything passes
dslc examples/fib.dsl             # run a program (from any directory)
dslc examples/gcd.dsl --emit=ir-opt --stats
```

**After editing `~/.zshrc` (or in a terminal opened before it was set up), load the shortcuts once with:**
```sh
source ~/.zshrc
```
New terminals pick them up automatically. The shortcuts live in a fenced `# >>> dslc project shortcuts >>>` block in `~/.zshrc`; delete that block to remove them.

> These shortcuts are just conveniences. The raw `cmake`/`ctest`/`dslc` commands (documented below) always work too — use whichever you prefer.

---

## 1. Background: how this project builds

This project uses **CMake**. CMake is not the compiler — it's a *build manager*. You describe the project once in `CMakeLists.txt` (what files exist, what to compile), and CMake generates the actual low-level build instructions (Makefiles) and drives the compiler for you. So building is always **two steps**:

1. **Configure** — CMake reads `CMakeLists.txt` and sets up a `build/` folder. Done once (and again only if `CMakeLists.txt` changes).
2. **Build** — CMake compiles the source into the `dslc` program and the test programs. Done every time you change code.

**CTest** ships with CMake and runs the test programs, reporting pass/fail.

---

## 2. Build commands

### 2.1 Configure the project
```sh
cmake -B build -S .
```
**What it does:** reads `CMakeLists.txt` and prepares the `build/` directory for compiling.

**Arguments:**
- `-B build` — the **B**uild directory. Where CMake puts all generated files and compiled output. `build` is just a folder name (conventional). Keeping build output in its own folder keeps your source clean.
- `-S .` — the **S**ource directory: where `CMakeLists.txt` lives. `.` means "the current folder."

**Run it:** once, after cloning or after cleaning. You do *not* need to re-run it every time you edit code.

**Optional extras:**
- `-G "Unix Makefiles"` — pick the **G**enerator (which build system CMake produces). `Unix Makefiles` is the plain, predictable default on macOS/Linux. We used this explicitly once to avoid an ambiguous multi-configuration setup.
- `-DCMAKE_BUILD_TYPE=Debug` — set a build type. `Debug` (our default) keeps debug info and no optimization on the *C++* compile; `Release` optimizes the compiler itself. (This is about compiling `dslc`, unrelated to `dslc`'s own `-O0/-O1`.) The `-D` prefix means "define a CMake variable."

### 2.2 Compile
```sh
cmake --build build
```
**What it does:** actually compiles the code in `build/`, producing the `dslc` binary and all test binaries.

**Arguments:**
- `--build build` — "build the project configured in the `build` directory." (Note: `--build` takes the build folder name, `build`.)

**Run it:** every time you change a `.cpp`/`.h` file. CMake only recompiles what changed, so it's fast.

**Optional extras:**
- `-j 8` — compile with 8 parallel jobs (faster on multi-core machines). Use `-j$(sysctl -n hw.ncpu)` to auto-pick your core count on macOS.
- `--target dslc` — build only the `dslc` binary (skip the tests). `--target test_lexer` builds just one test.
- `-v` — verbose: print the exact compiler command lines (useful when debugging build flags).

### 2.3 Clean rebuild from scratch
```sh
rm -rf build && cmake -B build -S . && cmake --build build
```
**What it does:** deletes the entire build folder and rebuilds everything fresh.

**Pieces:**
- `rm -rf build` — remove the `build` folder. `-r` = recursive (delete the folder and everything in it); `-f` = force (don't ask for confirmation). **Safe here** because `build/` is 100% regenerated from source.
- `&&` — "run the next command only if the previous one succeeded." Chains the three steps safely.

**Run it:** when the build acts strangely, or to prove the project builds cleanly from nothing (we used this to verify reproducibility).

---

## 3. Test commands

### 3.1 Run all tests
```sh
ctest --test-dir build --output-on-failure
```
**What it does:** runs all 7 test suites and prints a pass/fail summary.

**Arguments:**
- `--test-dir build` — where to find the compiled tests (the same `build` folder). Without this, CTest must be run from *inside* `build/`.
- `--output-on-failure` — if a test **fails**, print everything it printed (so you can see *why*). Passing tests stay quiet. Highly recommended — without it, a failure just says "Failed" with no detail.

**Run it:** after every build, to confirm nothing broke.

**Optional extras:**
- `-j 8` — run test suites in parallel.
- `-V` — verbose: show output of **all** tests, passing or failing.
- `--rerun-failed` — re-run only the tests that failed last time (fast iteration while fixing one thing).

### 3.2 Run one specific suite (filter by name)
```sh
ctest --test-dir build -R lexer --output-on-failure
```
**What it does:** runs only the tests whose name matches `lexer`.

**Arguments:**
- `-R <pattern>` — **R**un only tests matching this (regular-expression) pattern. `-R lexer` matches `test_lexer`. `-R "opt|e2e"` matches both `test_opt` and `test_e2e` (`|` means "or").
- The suite names are: `test_smoke`, `test_lexer`, `test_parser`, `test_sema`, `test_ir`, `test_opt`, `test_e2e`.

**Run it:** when you're working on one stage and only want its tests.

### 3.3 Run a test binary directly
```sh
./build/test_lexer
```
**What it does:** runs one compiled test program directly (bypassing CTest). It prints its own summary, e.g. `[lexer] 27 checks, 0 failures`, and exits `0` if everything passed.

**Pieces:**
- `./build/test_lexer` — the path to the binary. `./` means "in this location"; the binaries live in `build/`.

**Run it:** when you want the raw, detailed output of a single suite (each `CHECK` failure prints its file and line).

---

## 4. Running and inspecting programs (the `dslc` CLI)

The compiler binary is `./build/dslc`. General form:
```sh
./build/dslc <file.dsl> [options]
```
`<file.dsl>` is the path to a program written in our language (e.g. `examples/fib.dsl`).

### 4.1 Compile and run (the default)
```sh
./build/dslc examples/fib.dsl
```
**What it does:** runs the full pipeline (lex → parse → type-check → IR → **optimize** → bytecode → execute) and prints the program's output. With no `--emit`, running is the default — **there is no separate `--run` flag needed.** Optimization is **on** (`-O1`) unless you say otherwise.

### 4.2 `--emit=<stage>` — stop at a pipeline stage and print it
```sh
./build/dslc examples/fib.dsl --emit=ir
```
**What it does:** run the pipeline up to the named stage, print that stage's representation, and stop (don't execute). This is how you *see inside* the compiler — it's both the debugging tool and the demo tool.

**The `<stage>` values (in pipeline order):**
- `--emit=tokens` — the lexer's output: the list of tokens with line/column.
- `--emit=ast` — the parser's output: the syntax tree (as an indented outline).
- `--emit=sema` — runs the type-checker and reports success/errors (prints "ok: type-checked N functions").
- `--emit=ir` — the **unoptimized** intermediate representation (three-address instructions, basic blocks).
- `--emit=ir-opt` — the **optimized** IR (after the optimization passes). Compare with `--emit=ir` to see what the optimizer removed.
- `--emit=bc` — the final register **bytecode** the VM actually executes.

**Example — watch a program get optimized:**
```sh
./build/dslc examples/opt_demo.dsl --emit=ir       # before: ~8 instructions
./build/dslc examples/opt_demo.dsl --emit=ir-opt   # after:  ~2 instructions
```

### 4.3 `-O0` / `-O1` — turn the optimizer off / on
```sh
./build/dslc examples/gcd.dsl --emit=ir-opt -O0
```
**What it does:**
- `-O1` — optimizations **on** (the default). The "O" is for "Optimization"; `1` = level one.
- `-O0` — optimizations **off**. Level zero = do nothing. Useful to compare against `-O1`, or to see the raw IR the front end produced.

**Interview-relevant use:** the whole safety argument is "`-O0` and `-O1` must produce identical output." You can demonstrate it by hand:
```sh
./build/dslc examples/gcd.dsl -O0     # run unoptimized
./build/dslc examples/gcd.dsl -O1     # run optimized -> same output
```

### 4.4 `--stats` — show how much the optimizer removed
```sh
./build/dslc examples/gcd.dsl --stats --emit=ir-opt
```
**What it does:** prints the instruction count **before and after** optimization and the percentage removed, e.g.:
```
instructions: 18 -> 15  (16.7% removed)
```
This is the number that backs the "measurably reduced generated code size" claim.

### 4.5 Full flag reference

| Flag | Meaning | Default |
|------|---------|---------|
| `<file.dsl>` | the program to compile (required) | — |
| `--emit=tokens\|ast\|sema\|ir\|ir-opt\|bc` | print one stage and stop | (none → run) |
| `-O0` | optimizations off | — |
| `-O1` | optimizations on | **on** |
| `--stats` | print before/after instruction counts | off |
| *(no `--emit`)* | compile and execute on the VM | this is the default |

---

## 5. Cleanup command

```sh
rm -rf build
```
**What it does:** deletes the `build/` folder — all compiled binaries and CMake's cache (the ~28 MB of regenerable junk). Your source (~248 KB) is untouched.

**Arguments:** `-r` recursive (into the folder), `-f` force (no prompt).

**Run it:** to free space, or before a clean rebuild. **Consequence:** `./build/dslc` and the tests vanish until you rebuild (`cmake -B build -S . && cmake --build build`, ~10 seconds).

Also seen this session: `rm -rf build .DS_Store` — same thing plus deleting the macOS `.DS_Store` Finder-metadata file.

---

## 6. Git commands (version control)

We initialized git but committed nothing. Reference:

### 6.1 Start a repo (no commit)
```sh
git init
```
**What it does:** creates a hidden `.git/` folder that turns the directory into a git repository. **Commits nothing** — just makes committing *possible*.

### 6.2 See what git sees
```sh
git status            # full status
git status --short    # compact one-line-per-file view
```
**What it does:** shows which files are new/changed/staged. `??` next to a file means "untracked" (git sees it but isn't managing it yet).

### 6.3 Check that ignore rules work
```sh
git check-ignore -v build/dslc
```
**What it does:** tests whether a path would be ignored by `.gitignore`, and (with `-v`, verbose) shows *which rule* catches it. Output means "ignored"; no output means "not ignored." We used this to prove `build/` is ignored but source files aren't.

### 6.4 Commit (when you're ready — your call)
```sh
git add .                                  # stage everything (build/ auto-excluded by .gitignore)
git commit -m "message describing the change"
```
- `git add .` — stage all changes for the next commit. `.` = everything in the current folder tree.
- `git commit` — record the staged changes as a snapshot. `-m "..."` supplies the commit **m**essage inline (otherwise git opens an editor).

### 6.5 Push to GitHub (later)
```sh
git remote add origin <your-repo-url>
git push -u origin main
```
- `git remote add origin <url>` — link this local repo to a GitHub repo, nicknamed `origin`.
- `git push -u origin main` — upload the `main` branch to `origin`. `-u` remembers the link so future pushes are just `git push`.

---

## 7. Diagnostic commands we used while building

These aren't part of normal use, but they're how we *debugged* the build. Kept here so you understand what happened (see `docs/CHALLENGES.md` for the stories).

### 7.1 Check the toolchain exists
```sh
clang++ --version ; cmake --version ; make --version
```
**What it does:** prints the versions of the C++ compiler (`clang++`), the build manager (`cmake`), and the low-level build tool (`make`) — confirming they're installed before starting. `;` runs commands one after another regardless of success.

### 7.2 Measure disk usage
```sh
du -sh .          # total size of this folder
du -sh ./*        # size of each item, one per line
```
**What it does:** `du` = "disk usage." `-s` = summary (one total per item, not every sub-file); `-h` = human-readable sizes (KB/MB, not raw bytes). We used this to find that `build/` was 99% of the project's size.

### 7.3 Find stray files
```sh
find . -name ".DS_Store"
```
**What it does:** `find` searches a folder tree. `.` = start here; `-name ".DS_Store"` = match files with that exact name. We used it to locate macOS junk files.

### 7.4 See where the compiler looks for headers
```sh
clang++ -std=c++17 -E -x c++ -v /dev/null
```
**What it does:** makes the compiler print its **header search path** (the ordered list of folders it checks for `#include`s) without compiling anything real. This is how we found it was preferring a *broken* C++ standard-library folder over the real one (CHAL-002).
- `-std=c++17` — use the C++17 standard. `-E` — stop after preprocessing. `-x c++` — treat the input as C++. `-v` — verbose (print the search path). `/dev/null` — a special "empty file" input, so nothing is actually compiled.

### 7.5 Extract text from the résumé PDF
```sh
python3 -c "from pypdf import PdfReader; ..."
```
**What it does:** ran a tiny Python script (using the `pypdf` library) to pull the text out of the PDF, after the built-in image-based reader failed for lack of `poppler`. `python3 -c "<code>"` runs the given code string directly without a script file.

### 7.6 Bulk-rename a symbol across files
```sh
sed -i '' 's/Program/BCProgram/g' file1 file2 ...
```
**What it does:** `sed` find-and-replaces text in files. `s/Program/BCProgram/g` = **s**ubstitute `Program` with `BCProgram`, **g**lobally (every occurrence on each line). `-i ''` = edit the files **in place** (the `''` is required on macOS's BSD sed to mean "no backup file"). We used this for the `Program`→`BCProgram` rename (CHAL-005). ⚠️ Note from CHAL-006: macOS's `sed` does **not** support `\b` (word boundaries) — a plain pattern was needed.

---

## 8. Cheat sheet

```sh
# shell shortcuts (from ~/.zshrc — work from any folder; run `source ~/.zshrc` once first)
dslc-build                     # configure + compile
dslc-test                      # run all tests
dslc-rebuild                   # clean + build from scratch
dslc-clean                     # delete build/ (free space)
dslc examples/fib.dsl          # run the compiler from anywhere

# raw commands (equivalent, always work)
# build
cmake -B build -S .            # configure (once)   NOTE: the trailing "." is required
cmake --build build            # compile (after each edit)
rm -rf build && cmake -B build -S . && cmake --build build   # clean rebuild

# test
ctest --test-dir build --output-on-failure     # all tests
ctest --test-dir build -R sema                 # just the sema suite
./build/test_parser                            # one suite, raw output

# run / inspect
./build/dslc examples/fib.dsl                  # compile @ -O1 and run
./build/dslc PROG.dsl --emit=tokens            # see: tokens
./build/dslc PROG.dsl --emit=ast               #      syntax tree
./build/dslc PROG.dsl --emit=ir                #      unoptimized IR
./build/dslc PROG.dsl --emit=ir-opt --stats    #      optimized IR + size reduction
./build/dslc PROG.dsl --emit=bc                #      final bytecode
./build/dslc PROG.dsl -O0                       # run with optimizer off

# housekeeping
du -sh .                                        # project size
git status --short                              # what git sees
```
