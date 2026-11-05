# Guide — Phase 3: pipe objects and the global registry

**Goal:** two new system calls. `pipe_create()` returns the lowest free pipe ID (1–15); `pipe_destroy(id)` frees it. Both return −1 for an invalid ID or a full registry. No bytes move through pipes yet; that is Phase 4.

**Why:** build the data structures and the ID lookup on their own first, while every failure is easy to trace.

**What changes:** six places in `selfie.c`, all additions except Step 4. **Do Phases 0–2 first**, and do the steps **in order, from top to bottom**: each line number already accounts for the lines added by earlier phases and earlier steps. If a number doesn't match, use the search text.

| Step | Place | What it adds | Plan |
| --- | --- | --- | --- |
| 1 | syscall numbers | `SYSCALL_PIPE_CREATE`, `SYSCALL_PIPE_DESTROY`, two prototypes | R3.1 |
| 2 | `emit_bootstrapping` | the two new wrappers in the compiler's list | R3.1 |
| 3 | before `emit_open` | the wrapper functions `emit_pipe_create`, `emit_pipe_destroy` | R3.1 |
| 4 | `do_ecall` | which registers the new syscalls read | R3.8 |
| 5 | before `handle_system_call` | pipe object, registry, `pipe_create`, `pipe_destroy` | R3.2–R3.7 |
| 6 | `handle_system_call` | dispatch to the new functions | R3.7 |

All commands run in the **WSL terminal** inside `~/SELFIE-PIPES`; all edits happen in **VS Code**. Where a step says "insert", paste the lines on new lines at that spot; blank lines don't matter to the compiler.

**How a syscall travels in selfie** (useful for every step): a program calls `pipe_create()` → the compiler has emitted a small *wrapper* for that name (Steps 2–3) that puts the syscall number in register `a7` and runs `ecall` → the emulator's `do_ecall` checks the argument registers (Step 4) and raises a syscall exception → `handle_system_call` looks at `a7` (Step 6) and calls the implementation (Step 5) → the implementation puts the result in `a0` and moves the PC past the `ecall`.

---

## Step 0 — Start from Phase 2 and see the failure

```bash
cd ~/SELFIE-PIPES
git status
git log --oneline -3
```

- `git status` should say `nothing to commit, working tree clean`. If it lists changed files, commit or discard them first.
- `git log --oneline -3` lists the last 3 commits, newest first. The top one should be your Phase 2 commit (`Phase 2: fork skips unmapped pages when copying memory`).

```bash
./run_tests.sh 3
```

Expected ending, before the changes (everything above it passes):

```text
== phase 3
FAIL  p3_registry
      expected: ok 01 / ok 02 / ok 03 / ok 04 / ok 05 / ok 06 / ok 07 / ok 08  (exit code 0)
      got:      <no output>  (exit code none (selfie crashed, shell status 19))
      ./selfie: syntax error in tests/p3_registry.c in line 39: pipe_create undeclared
      ./selfie: warning in tests/p3_registry.c in line 39: type mismatch, uint64_t expected but undeclared found
      ./selfie: warning in tests/p3_registry.c in line 40: type mismatch, uint64_t expected but undeclared found

1 of 11 checks failed (phases 0..3).
```

Here selfie didn't really crash: its compiler refused the test because `pipe_create` doesn't exist yet, and it stopped with status 19 (a syntax error). The script reports any run without a "terminating" line the same way.

---

## Step 1 — Syscall numbers and prototypes

**Where:** near the top, after the existing syscall numbers (`SYSCALL_FORK = 215`, `SYSCALL_WAIT = 216`). Search for `DIRFD_AT_FDCWD corresponds`.

**Find this line** (line 1311):

```c
/* DIRFD_AT_FDCWD corresponds to AT_FDCWD in fcntl.h and
```

**Insert these 6 lines right before it** (nothing is removed):

```c
uint64_t SYSCALL_PIPE_CREATE  = 500;
uint64_t SYSCALL_PIPE_DESTROY = 503;

void emit_pipe_create();
void emit_pipe_destroy();

```

**What it does:**

- `SYSCALL_PIPE_CREATE = 500` and `SYSCALL_PIPE_DESTROY = 503` are the numbers a program puts in register `a7` to ask for these syscalls. They don't clash with existing ones (`fork` is 215, `wait` 216). 501 and 502 are left for `pipe_write` and `pipe_read` in Phase 4.
- The two prototypes declare the wrapper functions defined in Step 3. selfie compiles itself in one pass, so a function must be declared before the place that calls it (Step 2).

---

## Step 2 — Add the wrappers to the compiler's list

**Where:** inside `emit_bootstrapping`, in the list of `emit_...();` calls. Search for `// emit system call wrappers` (searching `emit_wait();` would also match its prototype near the top of the file).

**Find these lines** (lines 6353–6355):

```c
  emit_fork();
  emit_wait();
  emit_open();
```

**Insert these 2 lines right after them** (nothing is removed):

```c
  emit_pipe_create();
  emit_pipe_destroy();
```

**What it does:** every program selfie compiles gets a small built-in function for each syscall. This list is where they are generated. Without these two lines, programs can't call `pipe_create` or `pipe_destroy` (the error from Step 0).

---

## Step 3 — The wrapper functions

**Where:** just before the function `void emit_open() {`. Search for `void emit_open() {`.

**Find this line** (line 7858):

```c
void emit_open() {
```

**Insert these 21 lines right before it** (nothing is removed):

```c
void emit_pipe_create() {
  create_symbol_table_entry(GLOBAL_TABLE, string_copy("pipe_create"),
    0, PROCEDURE, UINT64_T, 0, code_size);

  emit_addi(REG_A7, REG_ZR, SYSCALL_PIPE_CREATE);
  emit_ecall();
  emit_jalr(REG_ZR, REG_RA, 0);
}

void emit_pipe_destroy() {
  create_symbol_table_entry(GLOBAL_TABLE, string_copy("pipe_destroy"),
    0, PROCEDURE, UINT64_T, 1, code_size);

  emit_load(REG_A0, REG_SP, 0); // pipe ID
  emit_addi(REG_SP, REG_SP, WORDSIZE);

  emit_addi(REG_A7, REG_ZR, SYSCALL_PIPE_DESTROY);
  emit_ecall();
  emit_jalr(REG_ZR, REG_RA, 0);
}

```

**What it does:** each wrapper writes a tiny RISC-U function into the compiled program.

- `create_symbol_table_entry(...)` registers the name (`"pipe_create"`), its return type (`UINT64_T`) and its number of arguments (0 for create, 1 for destroy), so the compiler accepts calls to it.
- `emit_load(REG_A0, REG_SP, 0)` + `emit_addi(REG_SP, ...)` (destroy only): the caller pushed the pipe ID on the stack; this moves it into register `a0` and pops it.
- `emit_addi(REG_A7, REG_ZR, SYSCALL_...)` puts the syscall number in `a7`.
- `emit_ecall()` traps into the kernel; `emit_jalr(REG_ZR, REG_RA, 0)` returns to the caller, with the result in `a0`.

This is the same pattern as the existing `emit_fork` (no arguments) and `emit_exit` (one argument).

---

## Step 4 — `do_ecall`: declare which registers the new syscalls read

**Where:** inside `void do_ecall()`, in the `else` branch near its end. Search for `if (*(registers + REG_A7) != SYSCALL_FORK) {`.

**Remove these 12 lines** (lines 10210–10221):

```c
	if (*(registers + REG_A7) != SYSCALL_FORK) {
		read_register(REG_A0);
		if (*(registers + REG_A7) != SYSCALL_EXIT) {
			if (*(registers + REG_A7) != SYSCALL_BRK) {
				if (*(registers + REG_A7) != SYSCALL_WAIT) {
					read_register(REG_A1);
					read_register(REG_A2);

					if (*(registers + REG_A7) == SYSCALL_OPENAT)
					read_register(REG_A3);
				}
			}
```

**Add these 18 lines in their place**:

```c
	if (*(registers + REG_A7) == SYSCALL_PIPE_CREATE)
		// no arguments, result in a0
		write_register(REG_A0);
	else if (*(registers + REG_A7) != SYSCALL_FORK) {
		read_register(REG_A0);
		if (*(registers + REG_A7) != SYSCALL_EXIT) {
			if (*(registers + REG_A7) != SYSCALL_BRK) {
				if (*(registers + REG_A7) != SYSCALL_WAIT) {
					// pipe_destroy reads only a0; pipe_write and pipe_read read a0-a2
					if (*(registers + REG_A7) != SYSCALL_PIPE_DESTROY) {
						read_register(REG_A1);
						read_register(REG_A2);

						if (*(registers + REG_A7) == SYSCALL_OPENAT)
						read_register(REG_A3);
					}
				}
			}
```

**Why this step exists:** this version of selfie checks, at every `ecall`, that the argument registers the syscall will read were written before. It assumes `a0`, `a1` and `a2` for any syscall it doesn't recognize. `pipe_create` takes no arguments and `pipe_destroy` takes one, so the emulator would complain that `a1`/`a2` were never written and stop the program.

**What changed:**

- A new first branch: for `pipe_create`, read nothing and only mark `a0` as written (it receives the result). Its `else if` keeps the old `SYSCALL_FORK` check, so everything else behaves as before.
- Inside, a new `if (... != SYSCALL_PIPE_DESTROY)` around the `a1`/`a2` reads: `pipe_destroy` reads only `a0`, like `exit` and `wait`.
- `pipe_write` and `pipe_read` (Phase 4) take three arguments, which is the default, so they need nothing here.
- The rest of the branch (the closing `}` lines and `write_register(REG_A0)` below) stays as it is.

If you skip this step, Step 7's test fails with `ecall: reading from uninitialized register a1`, then `… in presence of existing exception: uninitialized register a2`, and selfie stops with status 26.

---

## Step 5 — The pipe object, the registry, create and destroy

**Where:** just before the function `uint64_t handle_system_call(uint64_t* context) {`. Search for that line.

**Find this line** (line 11850):

```c
uint64_t handle_system_call(uint64_t* context) {
```

**Insert these 105 lines right before it** (nothing is removed):

```c
// -----------------------------------------------------------------
// ----------------------------- PIPES -----------------------------
// -----------------------------------------------------------------

// pipe object
// +---+---------------+
// | 0 | id            | global pipe ID (index into pipe_table)
// | 1 | buffer        | pointer to PIPE_CAPACITY bytes of ring storage
// | 2 | read index r  | slot of the next byte to read
// | 3 | write index w | slot of the next byte to write
// | 4 | count n       | number of stored bytes, 0 <= n <= PIPE_CAPACITY
// | 5 | readers head  | first context waiting for data
// | 6 | readers tail  | last context waiting for data
// | 7 | writers head  | first context waiting for space
// | 8 | writers tail  | last context waiting for space
// +---+---------------+

uint64_t PIPEENTRIES   = 9;
uint64_t PIPE_CAPACITY = 16; // bytes; small on purpose so blocking is easy to observe
uint64_t MAX_PIPES     = 16; // valid IDs are 1 .. MAX_PIPES - 1

uint64_t* pipe_table = (uint64_t*) 0; // global registry: pipe_table[id] -> pipe object

uint64_t get_pipe_id(uint64_t* pipe)     { return         *pipe; }
char*    get_pipe_buffer(uint64_t* pipe) { return (char*) *(pipe + 1); }
uint64_t get_pipe_r(uint64_t* pipe)      { return         *(pipe + 2); }
uint64_t get_pipe_w(uint64_t* pipe)      { return         *(pipe + 3); }
uint64_t get_pipe_n(uint64_t* pipe)      { return         *(pipe + 4); }

void set_pipe_id(uint64_t* pipe, uint64_t id)       { *pipe       = id; }
void set_pipe_buffer(uint64_t* pipe, char* buffer)  { *(pipe + 1) = (uint64_t) buffer; }
void set_pipe_r(uint64_t* pipe, uint64_t r)         { *(pipe + 2) = r; }
void set_pipe_w(uint64_t* pipe, uint64_t w)         { *(pipe + 3) = w; }
void set_pipe_n(uint64_t* pipe, uint64_t n)         { *(pipe + 4) = n; }

void syscall_return(uint64_t* context, uint64_t value) {
  *(get_regs(context) + REG_A0) = value;

  set_pc(context, get_pc(context) + INSTRUCTIONSIZE);
}

uint64_t* lookup_pipe(uint64_t id) {
  if (pipe_table == (uint64_t*) 0)
    return (uint64_t*) 0;
  else if (id == 0)
    return (uint64_t*) 0;
  else if (id >= MAX_PIPES)
    return (uint64_t*) 0;
  else
    return (uint64_t*) *(pipe_table + id);
}

void implement_pipe_create(uint64_t* context) {
  uint64_t id;
  uint64_t* pipe;

  if (pipe_table == (uint64_t*) 0)
    pipe_table = zmalloc(MAX_PIPES * sizeof(uint64_t*));

  id = 1;

  while (id < MAX_PIPES) {
    if (*(pipe_table + id) == 0) {
      // zeroed: r = w = n = 0 and both wait queues empty
      pipe = zmalloc(PIPEENTRIES * sizeof(uint64_t));

      set_pipe_id(pipe, id);
      set_pipe_buffer(pipe, (char*) zmalloc(round_up(PIPE_CAPACITY, sizeof(uint64_t))));

      *(pipe_table + id) = (uint64_t) pipe;

      syscall_return(context, id);

      return;
    }

    id = id + 1;
  }

  // registry full
  syscall_return(context, sign_shrink(-1, SYSCALL_BITWIDTH));
}

void implement_pipe_destroy(uint64_t* context) {
  uint64_t  id;
  uint64_t* pipe;

  id = *(get_regs(context) + REG_A0);

  pipe = lookup_pipe(id);

  if (pipe == (uint64_t*) 0) {
    syscall_return(context, sign_shrink(-1, SYSCALL_BITWIDTH));

    return;
  }

  // invalidate the ID: every later call with it returns -1
  *(pipe_table + id) = 0;

  // TODO: free pipe object and storage once selfie has a free list

  syscall_return(context, 0);
}

```

**What each part does:**

- **The layout comment** documents the pipe object: 9 words, like selfie documents its machine context. Fields 5–8 (the wait queues) are only used from Phase 5 on.
- **`PIPEENTRIES`, `PIPE_CAPACITY`, `MAX_PIPES`**: a pipe object has 9 words; each pipe stores up to 16 bytes (small on purpose, so blocking is easy to trigger in Phase 5); IDs go from 1 to 15.
- **`pipe_table`**: the global registry. Entry `i` holds a pointer to pipe `i`, or 0 if ID `i` is free. It starts as 0 (no table yet) and is created on the first `pipe_create`.
- **`get_pipe_...` / `set_pipe_...`**: selfie's C\* has no `struct`, so a "struct" is a block of words read and written through small functions. This is exactly how selfie handles its machine contexts.
- **`syscall_return(context, value)`**: puts the result in `a0` and moves the PC past the `ecall`, so the program continues after the call. Every pipe syscall ends with it.
- **`lookup_pipe(id)`**: returns the pipe for an ID, or 0 if the table doesn't exist yet, the ID is 0, the ID is too large (which also catches −1), or the slot is empty. The `else if` chain replaces `||`, which C\* doesn't have.
- **`implement_pipe_create`**: creates the table on first use (`zmalloc` returns memory filled with zeros), then looks for the lowest free ID. For it, it allocates a zeroed pipe object (so r = w = n = 0 and the queues are empty) and a zeroed 16-byte buffer, stores the pipe in the table and returns the ID. If all 15 IDs are taken, it returns −1. `sign_shrink(-1, SYSCALL_BITWIDTH)` is how selfie writes −1 into a 32-bit syscall result.
- **`implement_pipe_destroy`**: returns −1 for an invalid ID; otherwise it empties the table entry (so the ID can be reused) and returns 0. Waking processes blocked on the pipe comes in Phase 6. The memory isn't freed because selfie has no `free`; the TODO marks that.

---

## Step 6 — Dispatch the new syscalls

**Where:** inside `uint64_t handle_system_call(uint64_t* context)`, just before the `exit` branch. Search for `else if (a7 == SYSCALL_EXIT) {`.

**Find this line** (line 11979):

```c
  else if (a7 == SYSCALL_EXIT) {
```

**Insert these 4 lines right before it** (nothing is removed):

```c
  else if (a7 == SYSCALL_PIPE_CREATE)
    implement_pipe_create(context);
  else if (a7 == SYSCALL_PIPE_DESTROY)
    implement_pipe_destroy(context);
```

**What it does:** `handle_system_call` reads the syscall number from `a7` and calls the matching implementation. These two branches connect numbers 500 and 503 to the functions from Step 5.

Save the file (`Ctrl+S`).

---

## Step 7 — Test

```bash
./run_tests.sh 3
```

Expected ending (everything above it passes too):

```text
== phase 3
PASS  p3_registry

All 11 checks passed (phases 0..3).
```

What `p3_registry` proves, line by line:

- `ok 01`–`ok 02`: the first two pipes get IDs 1 and 2 (lowest free ID).
- `ok 03`–`ok 04`: destroying pipe 1 returns 0; destroying it again returns −1.
- `ok 05`: the next `pipe_create` reuses ID 1.
- `ok 06`–`ok 07`: IDs 0 and 99 are rejected with −1.
- `ok 08`: after 13 more creates (IDs 3–15), the registry is full and `pipe_create` returns −1.

In the test, −1 is compared as `MINUS1` (4294967295), because selfie shrinks syscall results to 32 bits.

If something fails:

- `pipe_create undeclared` (as in Step 0): Step 2 or 3 is missing, or the names are misspelled.
- `reading from uninitialized register a1`, status 26: Step 4 is missing or incomplete.
- `unknown system call 500` or `503`: Step 6 is missing.
- `FAIL builds with gcc` or `FAIL compiles itself`: a typo; the lines under the FAIL point to it. In Step 4, count the `{` and `}`.
- One `ok` line replaced by `FAIL NN`: compare that part of Step 5 with the guide.

---

## Step 8 — Review and save the change in Git

```bash
git diff --stat
```

Expected: `1 file changed, 150 insertions(+), 6 deletions(-)` (Step 4 changes 6 existing lines; everything else is new).

```bash
git diff
```

Read through the changes (`q` to exit). They should be the six places from Steps 1–6.

```bash
git add selfie.c
git commit -m "Phase 3: pipe objects, global registry, pipe_create and pipe_destroy"
git tag phase-3
git push origin main phase-3
```

- `git add` + `git commit` save the change as a new commit.
- `git tag phase-3` names this commit.
- `git push origin main phase-3` uploads the commit and the tag to GitHub.

---

## If you need to start over

```bash
git checkout phase-2 -- selfie.c
```

Brings back `selfie.c` exactly as it was after Phase 2. **This discards your uncommitted Phase 3 edits.**

---

**Next:** Phase 4 — moving bytes with `pipe_write` and `pipe_read`, without blocking yet (see the plan in `design/`).
