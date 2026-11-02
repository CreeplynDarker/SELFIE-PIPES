# Guide — Phase 4: moving bytes through a pipe, without blocking yet

**Goal:** two new system calls. `pipe_write(id, buffer, size)` copies bytes from the program into the pipe, and `pipe_read(id, buffer, size)` copies them out, in FIFO order, byte-exact, for buffers at any address. Each call moves as many bytes as possible right now (`min(requested, available)`) and returns that number. For now, when nothing can move (reading an empty pipe, writing a full one), the call returns 0 immediately; Phase 5 replaces exactly that with sleeping.

**Why:** get the data path and memory safety right in a single process, where every failure is deterministic and easy to trace, before adding blocking.

**What changes:** five places in `selfie.c`, all additions (208 new lines, nothing removed). **Do Phases 0–3 first**, following their guides, and do the steps **in order, from top to bottom**: each line number already accounts for the lines added by earlier phases and earlier steps. If a number doesn't match, use the search text.

| Step | Place | What it adds | Plan |
| --- | --- | --- | --- |
| 1 | syscall numbers | `SYSCALL_PIPE_WRITE`, `SYSCALL_PIPE_READ`, two prototypes | R4.1 |
| 2 | `emit_bootstrapping` | the two new wrappers in the compiler's list | R4.1 |
| 3 | after `emit_pipe_destroy` | the wrapper functions | R4.1 |
| 4 | after `implement_pipe_destroy` | memory access helper, `pipe_write`, `pipe_read` | R4.2–R4.6 |
| 5 | `handle_system_call` | dispatch to the new functions | R4.1 |

`do_ecall` needs no change this time: it already assumes three argument registers (`a0`–`a2`) for any syscall it doesn't know, and that is exactly what `pipe_write` and `pipe_read` take.

All commands run in the **WSL terminal** inside `~/SELFIE-PIPES`; all edits happen in **VS Code**. Where a step says **insert**, paste the new lines on new lines at that spot. When a block shown ends with an empty line, that empty line is part of it; empty lines don't matter to the compiler.

---

## Step 0 — Start from Phase 3 and see the failure

```bash
cd ~/SELFIE-PIPES
git status
git log --oneline -3
```

- `git status` should say `nothing to commit, working tree clean`. If it lists changed files, commit or discard them first.
- `git log --oneline -3` lists the last 3 commits, newest first. The top one should be your Phase 3 commit (`Phase 3: pipe objects, global registry, pipe_create and pipe_destroy`).

```bash
./run_tests.sh 4
```

Expected ending, before the changes (everything above it passes):

```text
== phase 4
FAIL  p4_transfer
      expected: ok 01 / ok 02 / … / ok 33  (exit code 0)
      got:      <no output>  (exit code none (selfie crashed, shell status 19))
      ./selfie: syntax error in tests/p4_transfer.c in line 44: pipe_write undeclared
      ./selfie: warning in tests/p4_transfer.c in line 44: type mismatch, undeclared expected but uint64_t found
      ./selfie: syntax error in tests/p4_transfer.c in line 46: pipe_read undeclared
FAIL  p4_temporary
      expected: ok 01 / ok 02 / ok 03  (exit code 0)
      got:      <no output>  (exit code none (selfie crashed, shell status 19))
      ./selfie: syntax error in tests/p4_temporary.c in line 41: pipe_read undeclared
      ./selfie: warning in tests/p4_temporary.c in line 41: type mismatch, undeclared expected but uint64_t found
      ./selfie: syntax error in tests/p4_temporary.c in line 42: pipe_write undeclared

2 of 13 checks failed (phases 0..4).
```

(The script prints all 33 `ok` lines in the `expected` line; they are shortened here.) As in Phase 3, status 19 isn't a crash: selfie's compiler refuses the tests because `pipe_write` and `pipe_read` don't exist yet.

---

## Step 1 — Syscall numbers and prototypes

**Where:** right after the Phase 3 lines `void emit_pipe_create();` / `void emit_pipe_destroy();`. Search for `void emit_pipe_destroy();`.

**Find these lines** (lines 1315–1316):

```c
void emit_pipe_destroy();

```

**Insert these 6 lines right after them** (nothing is removed):

```c
uint64_t SYSCALL_PIPE_WRITE = 501;
uint64_t SYSCALL_PIPE_READ  = 502;

void emit_pipe_write();
void emit_pipe_read();

```

**What it does:** numbers 501 and 502 are the values a program puts in register `a7` to ask for `pipe_write` and `pipe_read` (500 and 503 are create and destroy from Phase 3). The prototypes declare the wrapper functions of Step 3, because selfie compiles itself in one pass and Step 2 calls them before they are defined.

---

## Step 2 — Add the wrappers to the compiler's list

**Where:** inside `emit_bootstrapping`, right after the two lines you added in Phase 3. Search for `// emit system call wrappers` and look a few lines below it.

**Find this line** (line 6359):

```c
  emit_pipe_destroy();
```

**Insert these 2 lines right after it** (nothing is removed):

```c
  emit_pipe_write();
  emit_pipe_read();
```

**What it does:** every program selfie compiles gets a small built-in function per syscall; this list generates them. Without these lines, programs can't call `pipe_write` or `pipe_read` (the error from Step 0).

---

## Step 3 — The wrapper functions

**Where:** right after the end of the function `emit_pipe_destroy()` you added in Phase 3, before `void emit_open() {`. Search for `SYSCALL_PIPE_DESTROY);` (the match inside `emit_pipe_destroy`).

**Find these lines** (lines 7869–7873):

```c
  emit_addi(REG_A7, REG_ZR, SYSCALL_PIPE_DESTROY);
  emit_ecall();
  emit_jalr(REG_ZR, REG_RA, 0);
}

```

**Insert these 26 lines right after them** (nothing is removed):

```c
void emit_pipe_transfer(char* name, uint64_t syscall) {
  create_symbol_table_entry(GLOBAL_TABLE, string_copy(name),
    0, PROCEDURE, UINT64_T, 3, code_size);

  emit_load(REG_A0, REG_SP, 0); // pipe ID
  emit_addi(REG_SP, REG_SP, WORDSIZE);

  emit_load(REG_A1, REG_SP, 0); // *buffer
  emit_addi(REG_SP, REG_SP, WORDSIZE);

  emit_load(REG_A2, REG_SP, 0); // size
  emit_addi(REG_SP, REG_SP, WORDSIZE);

  emit_addi(REG_A7, REG_ZR, syscall);
  emit_ecall();
  emit_jalr(REG_ZR, REG_RA, 0);
}

void emit_pipe_write() {
  emit_pipe_transfer("pipe_write", SYSCALL_PIPE_WRITE);
}

void emit_pipe_read() {
  emit_pipe_transfer("pipe_read", SYSCALL_PIPE_READ);
}

```

**What it does:**

- `emit_pipe_transfer(name, syscall)` writes one wrapper for a syscall with three arguments. It registers the name with 3 arguments and return type `UINT64_T`, then emits code that takes the arguments the caller pushed on the stack and puts them in `a0` (pipe ID), `a1` (buffer address) and `a2` (size), puts the syscall number in `a7`, runs `ecall` and returns. It's the same pattern as the existing `emit_read`.
- `emit_pipe_write()` and `emit_pipe_read()` call it with their own name and number, so the code isn't written twice.

---

## Step 4 — Copying bytes between the program and the pipe

**Where:** right after the end of `implement_pipe_destroy` (Phase 3), before `uint64_t handle_system_call(uint64_t* context) {`. Search for `void implement_pipe_destroy(uint64_t* context) {` and go to the end of that function.

**Find these lines** (lines 11946–11948):

```c
  syscall_return(context, 0);
}

```

**Insert these 170 lines right after them** (nothing is removed):

```c
uint64_t* pipe_word = (uint64_t*) 0; // kernel scratch word for editing one byte of a guest word

uint64_t guest_word_address(uint64_t* context, uint64_t vaddr) {
  // virtual address of the guest word that holds byte vaddr, or 0 if inaccessible;
  // works for any byte alignment, unlike copy_buffer
  uint64_t waddr;

  waddr = vaddr - vaddr % WORDSIZE;

  if (is_virtual_address_valid(waddr, WORDSIZE) == 0)
    return 0;
  else if (is_data_stack_heap_address(context, waddr) == 0)
    return 0;

  if (is_virtual_address_mapped(get_pt(context), waddr) == 0) {
    // valid but never touched (e.g. fresh malloc memory): map it, like handle_page_fault
    if (pavailable() == 0)
      return 0;

    map_page(context, get_page_of_virtual_address(waddr), (uint64_t) palloc());
  }

  if (pipe_word == (uint64_t*) 0)
    pipe_word = zmalloc(sizeof(uint64_t));

  return waddr;
}

void implement_pipe_write(uint64_t* context) {
  uint64_t  id;
  uint64_t  vbuffer;
  uint64_t  size;
  uint64_t* pipe;
  uint64_t  waddr;
  uint64_t  k;
  uint64_t  more;
  uint64_t  bad_address;

  id      = *(get_regs(context) + REG_A0);
  vbuffer = *(get_regs(context) + REG_A1);
  size    = *(get_regs(context) + REG_A2);

  pipe = lookup_pipe(id);

  if (pipe == (uint64_t*) 0) {
    syscall_return(context, sign_shrink(-1, SYSCALL_BITWIDTH));

    return;
  } else if (size == 0) {
    syscall_return(context, 0);

    return;
  } else if (get_pipe_n(pipe) == PIPE_CAPACITY) {
    // PHASE 4 ONLY: nothing can move yet, return 0 (phase 5 blocks here instead)
    syscall_return(context, 0);

    return;
  }

  // move min(size, free space): a short write is allowed, callers loop
  k = 0;
  more = 1;
  bad_address = 0;

  while (more) {
    if (k == size)
      more = 0;
    else if (get_pipe_n(pipe) == PIPE_CAPACITY)
      more = 0;
    else {
      waddr = guest_word_address(context, vbuffer + k);

      if (waddr == 0) {
        bad_address = 1;

        more = 0;
      } else {
        // read one byte of the guest word through the kernel's scratch word
        *pipe_word = load_virtual_memory(get_pt(context), waddr);

        store_character(get_pipe_buffer(pipe), get_pipe_w(pipe),
          load_character((char*) pipe_word, (vbuffer + k) % WORDSIZE));

        set_pipe_w(pipe, (get_pipe_w(pipe) + 1) % PIPE_CAPACITY);
        set_pipe_n(pipe, get_pipe_n(pipe) + 1);

        k = k + 1;
      }
    }
  }

  if (bad_address)
    if (k == 0)
      k = sign_shrink(-1, SYSCALL_BITWIDTH);

  syscall_return(context, k);
}

void implement_pipe_read(uint64_t* context) {
  uint64_t  id;
  uint64_t  vbuffer;
  uint64_t  size;
  uint64_t* pipe;
  uint64_t  waddr;
  uint64_t  k;
  uint64_t  more;
  uint64_t  bad_address;

  id      = *(get_regs(context) + REG_A0);
  vbuffer = *(get_regs(context) + REG_A1);
  size    = *(get_regs(context) + REG_A2);

  pipe = lookup_pipe(id);

  if (pipe == (uint64_t*) 0) {
    syscall_return(context, sign_shrink(-1, SYSCALL_BITWIDTH));

    return;
  } else if (size == 0) {
    syscall_return(context, 0);

    return;
  } else if (get_pipe_n(pipe) == 0) {
    // PHASE 4 ONLY: nothing can move yet, return 0 (phase 5 blocks here instead)
    syscall_return(context, 0);

    return;
  }

  // move min(size, stored bytes): a short read is allowed, callers loop
  k = 0;
  more = 1;
  bad_address = 0;

  while (more) {
    if (k == size)
      more = 0;
    else if (get_pipe_n(pipe) == 0)
      more = 0;
    else {
      waddr = guest_word_address(context, vbuffer + k);

      if (waddr == 0) {
        bad_address = 1;

        more = 0;
      } else {
        // change one byte of the guest word, keep the other seven
        *pipe_word = load_virtual_memory(get_pt(context), waddr);

        store_character((char*) pipe_word, (vbuffer + k) % WORDSIZE,
          load_character(get_pipe_buffer(pipe), get_pipe_r(pipe)));

        store_virtual_memory(get_pt(context), waddr, *pipe_word);

        set_pipe_r(pipe, (get_pipe_r(pipe) + 1) % PIPE_CAPACITY);
        set_pipe_n(pipe, get_pipe_n(pipe) - 1);

        k = k + 1;
      }
    }
  }

  if (bad_address)
    if (k == 0)
      k = sign_shrink(-1, SYSCALL_BITWIDTH);

  syscall_return(context, k);
}

```

**What each part does:**

- **`pipe_word`** is one word of kernel memory used as a scratch space. selfie's memory functions read and write whole 8-byte words, so to touch a single byte the kernel loads the word that contains it, changes or reads one byte inside `pipe_word`, and (for reads) stores the word back.
- **`guest_word_address(context, vaddr)`** returns the address of the program's word that holds byte `vaddr`, or 0 if the program may not access it:
  - `vaddr - vaddr % WORDSIZE` rounds down to the start of the word, so any byte address works, aligned or not.
  - `is_virtual_address_valid` and `is_data_stack_heap_address` reject addresses outside the program's data, stack and heap (for example address 8, or the code).
  - If the page was never touched (fresh `malloc` memory), it maps a new zeroed page with `map_page(..., palloc())`, as `handle_page_fault` does.
  - It creates `pipe_word` on first use.
- **Why not `copy_buffer`**, which `read`/`write` use: it needs 8-byte-aligned addresses and copies whole words. A program that writes from `msg + 3`, or reads 5 bytes into an 8-byte buffer, would be rejected or would have 3 bytes after its buffer overwritten.
- **`implement_pipe_write`**:
  - Reads the three arguments from `a0`, `a1`, `a2` and looks up the pipe.
  - Invalid ID → −1. Size 0 → 0. These checks come first, so these calls never wait once Phase 5 exists.
  - Pipe full → the temporary Phase 4 branch: return 0 at once. The comment `PHASE 4 ONLY` marks the line Phase 5 replaces.
  - The loop moves one byte per turn: load the program's word, take the byte with `load_character`, store it in the ring at index `w` with `store_character`, then `w = (w + 1) % PIPE_CAPACITY` (wrap around after 15) and `n = n + 1`. It stops when `size` bytes moved or the pipe is full.
  - C\* has no `break`, so the loop runs `while (more)` and sets `more = 0` to stop.
  - A bad address stops the loop. If no byte moved, the result is −1 and the pipe is unchanged; otherwise it returns the bytes that did move.
- **`implement_pipe_read`** is the mirror image: it takes the byte at index `r` from the ring, puts it into the program's word inside `pipe_word`, stores the word back (the other 7 bytes stay as they were), and does `r = (r + 1) % PIPE_CAPACITY`, `n = n - 1`. A read of an empty pipe returns 0 for now.
- Both return with `syscall_return` (Phase 3): result in `a0`, PC moved past the `ecall`.

---

## Step 5 — Dispatch the new syscalls

**Where:** inside `handle_system_call`, right after the two Phase 3 branches. Search for `implement_pipe_destroy(context);`.

**Find this line** (line 12146):

```c
    implement_pipe_destroy(context);
```

**Insert these 4 lines right after it** (nothing is removed):

```c
  else if (a7 == SYSCALL_PIPE_WRITE)
    implement_pipe_write(context);
  else if (a7 == SYSCALL_PIPE_READ)
    implement_pipe_read(context);
```

**What it does:** connects syscall numbers 501 and 502 (in register `a7`) to the functions from Step 4.

Save the file (`Ctrl+S`).

---

## Step 6 — Test

```bash
./run_tests.sh 4
```

Expected ending (everything above it passes too):

```text
== phase 4
PASS  p4_transfer
PASS  p4_temporary

All 13 checks passed (phases 0..4).
```

What the tests prove (`p4_transfer` has 33 checks):

| Checks | What they prove |
| --- | --- |
| `ok 01`–`ok 05` | FIFO order, exact bytes, and a short read (asked for 100, got the 6 that existed) |
| `ok 06`–`ok 10` | a short write stops at the 16-byte capacity |
| `ok 11`–`ok 13` | writing from an unaligned address (`msg + 3`) |
| `ok 14`–`ok 16` | a 1-byte read leaves the other 7 bytes of the word alone |
| `ok 17`–`ok 19` | reading into an unaligned address (`buf + 5`) |
| `ok 20`–`ok 22` | reading into a heap page the program never touched |
| `ok 23`–`ok 27` | a bad address returns −1 and no byte is lost |
| `ok 28`–`ok 31` | size 0 returns 0; bad IDs return −1 |
| `ok 32`–`ok 33` | a destroyed pipe can't be used |
| `p4_temporary` | reading an empty pipe and writing a full one return 0 (temporary behavior) |

If something fails:

- `pipe_write undeclared` or `pipe_read undeclared`: Step 2 or 3 is missing or misspelled.
- `unknown system call 501` or `502`: Step 5 is missing.
- One `FAIL NN` inside `p4_transfer`: compare the matching part of Step 4. Checks 11–22 point to `guest_word_address` or the byte handling with `pipe_word`; 23–27 to the `bad_address` handling.
- `FAIL builds with gcc` or `FAIL compiles itself`: a typo; the lines under the FAIL point to it.

---

## Step 7 — Review and save the change in Git

```bash
git diff --stat
```

Expected: `1 file changed, 208 insertions(+)`.

```bash
git diff
```

Read through the changes (`q` to exit): five blocks, matching Steps 1–5.

```bash
git add selfie.c
git commit -m "Phase 4: pipe_write and pipe_read move bytes, no blocking yet"
git tag phase-4
git push origin main phase-4
```

- `git add` + `git commit` save the change as a new commit.
- `git tag phase-4` names this commit.
- `git push origin main phase-4` uploads the commit and the tag to GitHub.

---

## If you need to start over

```bash
git checkout phase-3 -- selfie.c
```

Brings back `selfie.c` exactly as it was after Phase 3. **This discards your uncommitted Phase 4 edits.**

---

**Next:** `GUIDE_P5.md` — blocking, wakeups and deadlock detection.
