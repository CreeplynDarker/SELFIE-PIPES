# Guide — Phase 5: blocking, wakeups and deadlock detection

**Goal:** a read of an empty pipe, or a write to a full one, puts the process to sleep instead of returning 0. The next transfer in the other direction wakes it, and it tries again. If every remaining process is asleep, selfie reports a deadlock (exit code 28) instead of hanging.

**Why:** this is the core of the pipe architecture: a producer and a consumer that wait for each other without wasting CPU.

**How it works, in one paragraph:** when a call can't proceed, the kernel adds the process to the pipe's wait queue and calls the base code's `block_context()`, which moves it off the list of runnable processes. The trick is that the kernel **does not move the program counter past the `ecall`**. When another process moves bytes, the kernel calls `unblock_context()` on everyone in the queue. The next time a woken process runs, it executes the same `ecall` again, with the same arguments still in `a0`–`a2`, so it rechecks the pipe from scratch: a free `while (!condition) wait();`. No lock is needed, because each syscall handler runs to the end before any other process can run.

**What changes:** twelve small places in `selfie.c` (65 lines added, 5 changed). **Do Phases 0–4 first**, and do the steps **in order, from top to bottom**: each line number already accounts for earlier phases and earlier steps. If a number doesn't match, use the search text.

| Steps | Place | What they do | Plan |
| --- | --- | --- | --- |
| 1–6 | machine context | new field 38, "next waiter", to link a pipe's waiting processes | R5.1 |
| 7 | pipe constants | queue offsets and the `debug_pipes` switch | R5.2, R5.8 |
| 8 | before `implement_pipe_create` | `enqueue_waiter` (sleep) and `wake_all_waiters` (wake) | R5.2, R5.3 |
| 9–10 | `implement_pipe_write` | sleep when full; wake readers after writing | R5.4, R5.5 |
| 11–12 | `implement_pipe_read` | sleep when empty; wake writers after reading | R5.4, R5.5 |

Deadlock reporting already exists from Phase 1 (`report_deadlock` in `mipster`); this phase only starts using it.

All commands run in the **WSL terminal** inside `~/SELFIE-PIPES`; all edits happen in **VS Code**. Where a step says **insert**, paste the new lines on new lines at that spot. When a block shown ends with an empty line, that empty line is part of it; empty lines don't matter to the compiler.

---

## Step 0 — Start from Phase 4 and see the failures

```bash
cd ~/SELFIE-PIPES
git status
git log --oneline -3
```

- `git status` should say `nothing to commit, working tree clean`. If it lists changed files, commit or discard them first.
- `git log --oneline -3` lists the last 3 commits, newest first. The top one should be your Phase 4 commit (`Phase 4: pipe_write and pipe_read move bytes, no blocking yet`).

```bash
./run_tests.sh 5
```

Expected ending, before the changes (Phases 0–3 pass above it):

```text
== phase 4
PASS  p4_transfer
FAIL  p4_temporary
      expected: deadlock: 1 context(s) blocked and none ready  (exit code 28)
      got:      ok 01 / ok 02 / ok 03  (exit code 0)
== phase 5
PASS  p5_stream
FAIL  p5_retry
      expected: r2=A / r1=B  (exit code 0)
      got:      r1=r2=  (exit code 0)
FAIL  p5_deadlock
      expected: deadlock: 1 context(s) blocked and none ready  (exit code 28)
      got:      <no output>  (exit code 0)

3 of 16 checks failed (phases 0..5).
```

What each result means:

- **`p4_temporary` fails on purpose.** Asked for phase 5, the script expects the Phase 5 behavior: reading an empty pipe must now sleep, and with no writer that is a deadlock. Today it still returns 0.
- **`p5_stream` already passes, but wastefully:** the reader gets 0 bytes again and again and keeps asking until the writer has written. That's busy waiting. After this phase the reader sleeps instead.
- **`p5_retry`:** two readers ask for one byte before anything was written; both get 0 bytes at once and print an empty result.
- **`p5_deadlock`:** a reader with no writer gets 0 and exits normally, instead of being reported as a deadlock.

**Important from now on:** always run `./run_tests.sh 5` (or `6` later), not `./run_tests.sh 4`. The script decides what `p4_temporary` should do from the number you give, and after this phase its Phase 4 expectation (return 0) is no longer true.

---

## Step 1 — Document context field 38

**Where:** the comment that describes the machine context, near the top. Search for `| 37 | child pid`.

**Find this line** (line 2240):

```c
// | 37 | child pid				| pid of exited child
```

**Insert these 1 line right after it** (nothing is removed):

```c
// | 38 | next waiter     | next context in the same pipe wait queue
```

**Why:** selfie documents every field of a machine context in this table. Field 38 is new: it links the processes waiting on the same pipe.

---

## Step 2 — Make room for field 38

**Where:** right below that table. Search for `CONTEXTENTRIES = 38`.

**Remove this line** (line 2246):

```c
uint64_t CONTEXTENTRIES = 38;
```

**Add this line in its place:**

```c
uint64_t CONTEXTENTRIES = 39;
```

**Why:** `CONTEXTENTRIES` is how many 8-byte words each context gets (`allocate_context` multiplies by it). The base has fields 0–37, so 38 words; field 38 needs 39.

---

## Step 3 — Address helper for field 38

**Where:** in the list of field-address functions. Search for `uint64_t child_pid(uint64_t* context)`.

**Find this line** (line 2303):

```c
uint64_t child_pid(uint64_t* context) { return (uint64_t) (context + 37); }
```

**Insert these 1 line right after it** (nothing is removed):

```c
uint64_t next_waiter(uint64_t* context) { return (uint64_t) (context + 38); }
```

**Why:** selfie has one such function per field (the field's address). This keeps field 38 consistent with the others.

---

## Step 4 — Getter for field 38

**Where:** in the list of `get_` functions. Search for `uint64_t get_child_pid (uint64_t* context)` (note the space before the `(`, as in the base code).

**Find this line** (line 2345):

```c
uint64_t get_child_pid (uint64_t* context) { return *(context + 37); }
```

**Insert these 1 line right after it** (nothing is removed):

```c
uint64_t* get_next_waiter(uint64_t* context) { return (uint64_t*) *(context + 38); }
```

**Why:** `get_next_waiter(context)` returns the next process in the same pipe queue, or 0 at the end of the queue.

---

## Step 5 — Setter for field 38

**Where:** in the list of `set_` functions. Search for `void set_child_pid(uint64_t* context`.

**Find this line** (line 2387):

```c
void set_child_pid(uint64_t* context, uint64_t child_pid) { *(context + 37) = child_pid; }
```

**Insert these 1 line right after it** (nothing is removed):

```c
void set_next_waiter(uint64_t* context, uint64_t* waiter) { *(context + 38) = (uint64_t) waiter; }
```

**Why:** `set_next_waiter(context, waiter)` links a process to the next one in the queue.

---

## Step 6 — Start field 38 at 0

**Where:** at the end of `init_context`, after the lines you added in Phase 1. Search for `set_child_pid(context, 0);`.

**Find this line** (line 11250):

```c
  set_child_pid(context, 0);
```

**Insert these 1 line right after it** (nothing is removed):

```c
  set_next_waiter(context, (uint64_t*) 0);
```

**Why:** contexts are reused after a process exits, and `smalloc` doesn't clear memory. Every new process must start outside any queue.

**Why a new field at all:** the base's `block_context()` moves a blocked process onto `blocked_contexts` using the context's own next/prev fields (0 and 1). Those fields are busy while the process sleeps, so the pipe's queue needs a separate link.

---

## Step 7 — Queue offsets and the debug switch

**Where:** in the Phase 3 pipe section, right after `pipe_table`. Search for `uint64_t* pipe_table = (uint64_t*) 0;`.

**Find these lines** (lines 11870–11871):

```c
uint64_t* pipe_table = (uint64_t*) 0; // global registry: pipe_table[id] -> pipe object

```

**Insert these 5 lines right after them** (nothing is removed):

```c
uint64_t PIPE_READERS = 5; // offset of the readers queue (head, tail)
uint64_t PIPE_WRITERS = 7; // offset of the writers queue (head, tail)

uint64_t debug_pipes = 0;

```

**What it does:**

- A pipe object stores two queues (see its layout comment from Phase 3): the readers queue in words 5 (head) and 6 (tail), the writers queue in words 7 and 8. `PIPE_READERS = 5` and `PIPE_WRITERS = 7` say where each queue starts, so one pair of functions (Step 8) serves both.
- `debug_pipes = 0`: set it to 1 to print every sleep and wakeup (used in Step 13).

---

## Step 8 — Sleep and wake

**Where:** right after the end of `lookup_pipe`, before `void implement_pipe_create(uint64_t* context) {`. Search for `uint64_t* lookup_pipe(uint64_t id) {` and go to the end of that function.

**Find these lines** (lines 11903–11905):

```c
    return (uint64_t*) *(pipe_table + id);
}

```

**Insert these 44 lines right after them** (nothing is removed):

```c
void enqueue_waiter(uint64_t* pipe, uint64_t queue, uint64_t* context) {
  // queue is PIPE_READERS or PIPE_WRITERS: head at pipe + queue, tail at pipe + queue + 1;
  // the queue links contexts through their own next-waiter field, because
  // block_context reuses the next/prev fields for the blocked_contexts list
  set_next_waiter(context, (uint64_t*) 0);

  if (*(pipe + queue) == 0)
    *(pipe + queue) = (uint64_t) context;
  else
    set_next_waiter((uint64_t*) *(pipe + queue + 1), context);

  *(pipe + queue + 1) = (uint64_t) context;

  // off used_contexts, onto blocked_contexts: the scheduler can no longer pick it
  block_context(context);

  if (debug_pipes)
    printf("%s: pid %lu blocks on pipe %lu\n", selfie_name, get_pid(context), get_pipe_id(pipe));
}

void wake_all_waiters(uint64_t* pipe, uint64_t queue) {
  uint64_t* context;
  uint64_t* next;

  context = (uint64_t*) *(pipe + queue);

  while (context != (uint64_t*) 0) {
    next = get_next_waiter(context);

    set_next_waiter(context, (uint64_t*) 0);

    // back onto used_contexts as READY; its ecall runs again when it is scheduled
    unblock_context(context);

    if (debug_pipes)
      printf("%s: pipe %lu wakes pid %lu\n", selfie_name, get_pipe_id(pipe), get_pid(context));

    context = next;
  }

  *(pipe + queue)     = 0;
  *(pipe + queue + 1) = 0;
}

```

**What each function does:**

- **`enqueue_waiter(pipe, queue, context)`** adds a process to the end of a queue and puts it to sleep:
  - `*(pipe + queue)` is the queue's head and `*(pipe + queue + 1)` its tail. If the queue is empty, the process becomes the head; otherwise the current tail links to it with `set_next_waiter`. Either way it becomes the new tail. That keeps the queue in arrival order (FIFO).
  - `block_context(context)` (base code, repaired in Phase 1) moves it off the runnable list and marks it BLOCKED, so the scheduler can't pick it.
  - With `debug_pipes` on, it prints `pid N blocks on pipe P`.
- **`wake_all_waiters(pipe, queue)`** wakes everyone in a queue:
  - For each process it remembers the next one, clears the link, and calls `unblock_context(context)` (base code, repaired in Phase 1), which puts it back on the runnable list as READY.
  - Then it empties the queue (head and tail = 0).
  - Waking everyone is safe: each woken process re-runs its `ecall` and rechecks; one that finds nothing to do simply goes back to sleep.

---

## Step 9 — `pipe_write`: sleep when the pipe is full

**Where:** inside `implement_pipe_write`. Search for `== PIPE_CAPACITY) {` (it appears only once, in `implement_pipe_write`); the two Phase 4 lines marked `PHASE 4 ONLY` follow it.

**Remove these 3 lines** (lines 12055–12057):

```c
  } else if (get_pipe_n(pipe) == PIPE_CAPACITY) {
    // PHASE 4 ONLY: nothing can move yet, return 0 (phase 5 blocks here instead)
    syscall_return(context, 0);
```

**Add these 3 lines in their place:**

```c
  } else if (get_pipe_n(pipe) == PIPE_CAPACITY) {
    // full: sleep WITHOUT advancing the PC, so this ecall runs again after a wakeup
    enqueue_waiter(pipe, PIPE_WRITERS, context);
```

**What changed:** the first line stays. The two temporary Phase 4 lines (`return 0`) are replaced by `enqueue_waiter(pipe, PIPE_WRITERS, context);`. The `return;` that follows stays as it is. Note what's **not** there: no `syscall_return`. The PC stays on the `ecall` and `a0` keeps the pipe ID, so when the writer wakes up it repeats the whole `pipe_write` call.

---

## Step 10 — `pipe_write`: wake the readers after writing

**Where:** still in `implement_pipe_write`, right after the `while (more)` loop, before `if (bad_address)`. Search for `get_pipe_n(pipe) + 1);` and go down to the end of the loop.

**Find these lines** (lines 12087–12093):

```c
        set_pipe_n(pipe, get_pipe_n(pipe) + 1);

        k = k + 1;
      }
    }
  }

```

**Insert these 3 lines right after them** (nothing is removed):

```c
  if (k > 0)
    wake_all_waiters(pipe, PIPE_READERS);

```

**Why:** if at least one byte went into the pipe, readers waiting for data can now make progress. The `if (k > 0)` avoids waking them for nothing.

---

## Step 11 — `pipe_read`: sleep when the pipe is empty

**Where:** inside `implement_pipe_read`. Search for `PHASE 4 ONLY`; after Step 9 the only match left is in `implement_pipe_read`.

**Remove these 2 lines** (lines 12129–12130):

```c
    // PHASE 4 ONLY: nothing can move yet, return 0 (phase 5 blocks here instead)
    syscall_return(context, 0);
```

**Add these 2 lines in their place:**

```c
    // empty: sleep WITHOUT advancing the PC, so this ecall runs again after a wakeup
    enqueue_waiter(pipe, PIPE_READERS, context);
```

**What changed:** the mirror of Step 9. The line above (`} else if (get_pipe_n(pipe) == 0) {`) and the `return;` below stay. A reader of an empty pipe joins the readers queue and sleeps, with the PC still on its `ecall`.

---

## Step 12 — `pipe_read`: wake the writers after reading

**Where:** still in `implement_pipe_read`, right after its `while (more)` loop. Search for `get_pipe_n(pipe) - 1);` and go down to the end of the loop.

**Find these lines** (lines 12162–12168):

```c
        set_pipe_n(pipe, get_pipe_n(pipe) - 1);

        k = k + 1;
      }
    }
  }

```

**Insert these 3 lines right after them** (nothing is removed):

```c
  if (k > 0)
    wake_all_waiters(pipe, PIPE_WRITERS);

```

**Why:** if at least one byte left the pipe, there is free space, so writers waiting for space can make progress.

Save the file (`Ctrl+S`).

---

## Step 13 — Test

```bash
./run_tests.sh 5
```

Expected ending (Phases 0–3 pass above it):

```text
== phase 4
PASS  p4_transfer
PASS  p4_temporary
== phase 5
PASS  p5_stream
PASS  p5_retry
PASS  p5_deadlock

All 16 checks passed (phases 0..5).
```

What the tests prove:

- **`p5_stream`:** a child writes a 58-byte message into the 16-byte pipe while the parent reads 5 bytes at a time. Both sides block and wake many times, and the message arrives exactly: `Pipes carry bytes from one process to another, in order.`
- **`p5_retry`:** prints `r2=A`, then `r1=B`. Two readers sleep on one pipe; each written byte wakes both, but only one gets it and the other goes back to sleep.
- **`p5_deadlock`:** a reader with no writer is reported as `deadlock: 1 context(s) blocked and none ready`, exit code 28.
- **`p4_temporary`:** now ends in the same deadlock message. That proves the temporary `return 0` branches are gone.

**See the sleeping and waking yourself** (optional, but worth doing once). In `selfie.c`, change `uint64_t debug_pipes = 0;` to `uint64_t debug_pipes = 1;` and save. Then:

```bash
gcc -w -O2 -m64 -D'uint64_t=unsigned long' selfie.c -o selfie
./selfie -c tests/p5_retry.c -m 2 | sed -n '/>>>>/,/<<<</p'
```

- The first command builds selfie, as `run_tests.sh` does.
- The second compiles and runs one test. `sed -n '/>>>>/,/<<<</p'` keeps only the part between selfie's `>>>>` and `<<<<` lines, which is the program's run.

Expected lines (plus selfie's `>>>>`/`<<<<` lines and blank lines):

```text
./selfie: pid 1 blocks on pipe 1
./selfie: pid 2 blocks on pipe 1
./selfie: pipe 1 wakes pid 1
./selfie: pipe 1 wakes pid 2
./selfie: pid 1 blocks on pipe 1
r2=A
./selfie: pipe 1 wakes pid 1
r1=B
```

Both readers block, the first byte wakes both, reader 2 gets `A`, reader 1 finds the pipe empty and blocks again, and the second byte wakes it for `B`. **Set `debug_pipes` back to 0** and save before you continue; with it on, the extra lines make the tests fail.

If something fails:

- A test runs for minutes and ends `timed out`: a woken process never retries or is never woken. Check Steps 8, 10 and 12.
- `p5_retry` prints only one line: the second reader was never woken, or was woken but never put back to sleep. Check Step 8 (`wake_all_waiters` must walk the whole queue).
- `p5_deadlock` exits with code 0: Step 11 is missing (the read still returns 0).
- Selfie crashes (status 139): usually a broken queue link. Check Steps 1–6 and that `CONTEXTENTRIES` is 39.

---

## Step 14 — Review and save the change in Git

```bash
git diff --stat
```

Expected: `1 file changed, 65 insertions(+), 5 deletions(-)`. If the numbers differ by one, check that `debug_pipes` is back to `0`.

```bash
git diff
```

Read through the changes (`q` to exit): twelve small blocks, matching Steps 1–12.

```bash
git add selfie.c
git commit -m "Phase 5: blocking pipes with wait queues, wakeups and deadlock detection"
git tag phase-5
git push origin main phase-5
```

- `git add` + `git commit` save the change as a new commit.
- `git tag phase-5` names this commit.
- `git push origin main phase-5` uploads the commit and the tag to GitHub.

---

## If you need to start over

```bash
git checkout phase-4 -- selfie.c
```

Brings back `selfie.c` exactly as it was after Phase 4. **This discards your uncommitted Phase 5 edits.**

---

**One side effect to mention in your report:** each retried `ecall` counts again in the context's syscall counter (`ec_syscall`), so a process that slept counts more syscalls than it made.

**Next:** Phase 6 — `pipe_destroy` wakes the sleepers, plus the final regression (see the plan in `design/`).
