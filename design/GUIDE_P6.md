# Guide — Phase 6: destroying a pipe wakes its sleepers, and the final check

**Goal:** when a pipe is destroyed, every process sleeping on it wakes up, retries its call, finds the pipe gone and gets −1. Then confirm the whole pipe architecture end to end: all 18 checks, plus pipes working while selfie runs inside itself.

**Why:** after Phase 5, `pipe_destroy` frees the ID but leaves sleeping processes in the pipe's queues. Nobody will ever write to or read from that pipe again, so they would sleep forever (selfie reports it as a deadlock).

**What changes:** one place in `selfie.c`, inside `implement_pipe_destroy`: 2 lines become 5 (one comment line reworded, three lines added). **Do Phases 0–5 first**, following their guides; the line numbers below assume they are done.

All commands run in the **WSL terminal** inside `~/SELFIE-PIPES`; all edits happen in **VS Code**.

---

## Step 0 — Start from Phase 5 and see the problem

```bash
cd ~/SELFIE-PIPES
git status
git log --oneline -3
```

- `git status` should say `nothing to commit, working tree clean`. If it lists changed files, commit or discard them first.
- `git log --oneline -3` lists the last 3 commits, newest first. The top one should be your Phase 5 commit (`Phase 5: blocking pipes with wait queues, wakeups and deadlock detection`).

Now the **negative check**:

```bash
./run_tests.sh 6
```

Expected ending, before the change (Phases 0–5 pass above it):

```text
== phase 6
FAIL  p6_destroy_reader
      expected: ok 01 / ok 02 / ok 03  (exit code 0)
      got:      ok 01 / ok 03 / deadlock: 1 context(s) blocked and none ready  (exit code 28)
FAIL  p6_destroy_writer
      expected: ok 01 / ok 03 / ok 02  (exit code 0)
      got:      ok 01 / ok 02 / deadlock: 1 context(s) blocked and none ready  (exit code 28)

2 of 18 checks failed (phases 0..6).
```

What happens in each test today:

- **`p6_destroy_reader`:** a child blocks reading an empty pipe. The parent destroys the pipe (`ok 01`) and checks that destroying it again fails (`ok 03`). The child is never woken, so its `ok 02` never prints, and when the parent exits, the sleeping child is reported as a deadlock.
- **`p6_destroy_writer`:** a child fills the pipe with 16 bytes (`ok 01`), then blocks trying to write one more. The parent destroys the pipe (`ok 02`). The child is never woken, so its `ok 03` never prints, and the run ends in a deadlock.

Both failures prove the tests can detect a missing wakeup.

---

## Step 1 — `implement_pipe_destroy`: wake both queues

**Where:** inside `void implement_pipe_destroy(uint64_t* context)`, which you added in Phase 3. Search for `// invalidate the ID`.

**Remove these 2 lines** (lines 11995–11996):

```c
  // invalidate the ID: every later call with it returns -1
  *(pipe_table + id) = 0;
```

**Add these 5 lines in their place:**

```c
  // invalidate the ID first: woken waiters retry their ecall and get -1
  *(pipe_table + id) = 0;

  wake_all_waiters(pipe, PIPE_READERS);
  wake_all_waiters(pipe, PIPE_WRITERS);
```

**What changed:**

- The comment is reworded to say what now matters: the ID is invalidated first, and woken processes then get −1.
- `*(pipe_table + id) = 0;` is the same line as before: it removes the pipe from the registry.
- `wake_all_waiters(pipe, PIPE_READERS);` and `wake_all_waiters(pipe, PIPE_WRITERS);` (from Phase 5) move every process waiting on this pipe, readers and writers, back onto the runnable list as READY.

**Why this is all it takes:** a woken process didn't finish its call. Its PC is still on the `ecall` (Phase 5), so when it runs again it repeats `pipe_read` or `pipe_write` with the same pipe ID. The lookup now finds an empty registry slot, and the call takes the "invalid ID → −1" path you wrote in Phase 4. No special case is needed in read or write. The woken processes only run after this handler has finished, so by then the ID is already invalid.

The `TODO` below (freeing the pipe's memory) stays: selfie has no `free`, so the memory isn't returned.

Save the file (`Ctrl+S`).

---

## Step 2 — Test everything

```bash
./run_tests.sh 6
```

Expected output, the complete suite:

```text
== gate
PASS  builds with gcc
PASS  compiles itself without errors or warnings
PASS  runs itself (selfie-on-selfie)
== phase 0
PASS  p0_write
== phase 1
PASS  p1_exit
PASS  p1_deadlock
PASS  p1_two_waiters
== phase 2
PASS  p2_fork
PASS  p2_exit
PASS  p2_fork_gap
== phase 3
PASS  p3_registry
== phase 4
PASS  p4_transfer
PASS  p4_temporary
== phase 5
PASS  p5_stream
PASS  p5_retry
PASS  p5_deadlock
== phase 6
PASS  p6_destroy_reader
PASS  p6_destroy_writer

All 18 checks passed (phases 0..6).
```

What the Phase 6 tests prove:

- **`p6_destroy_reader`** prints `ok 01`, `ok 02`, `ok 03`: the parent's destroy returns 0 (`ok 01`); the sleeping reader wakes and its `pipe_read` returns −1 (`ok 02`); destroying the same ID again returns −1 (`ok 03`).
- **`p6_destroy_writer`** prints `ok 01`, `ok 03`, `ok 02`: the child's first write stores 16 bytes (`ok 01`); its second write sleeps because the pipe is full; the parent destroys the pipe; the woken writer's call returns −1 (`ok 03`); and the parent's own `ok 02` (destroy returned 0) prints right after. The order looks odd, but it's correct: this base switches process after every syscall, and the woken child is first in line.
- Every earlier test still passes, so nothing from Phases 0–5 broke.

If a Phase 6 test still ends in a deadlock, one of the two `wake_all_waiters` lines is missing or uses the wrong queue: readers need `PIPE_READERS`, writers `PIPE_WRITERS`.

---

## Step 3 — The final check: pipes while selfie runs itself

`run_tests.sh` already checked that selfie can still compile and run itself. This last check runs the pipe tests **inside** that second selfie, so the new syscalls also work one level up. `run_tests.sh` has already built `./selfie`, so run:

```bash
./selfie -c selfie.c -m 64 -c tests/p5_stream.c -m 2 | grep -E "^Pipes|terminating"
```

- `./selfie -c selfie.c -m 64` compiles `selfie.c` with selfie and runs the result with 64 MB of memory: a selfie running inside selfie.
- `-c tests/p5_stream.c -m 2` makes that inner selfie compile and run the pipe test.
- `grep -E "^Pipes|terminating"` keeps only the test's message and the two "terminating" lines.

Expected (it takes about a second):

```text
Pipes carry bytes from one process to another, in order.
> selfie.c: 64-bit mipster terminating 64-bit RISC-U binary tests/p5_stream.c with exit code 0
./selfie: 64-bit mipster terminating 64-bit RISC-U binary selfie.c with exit code 0
```

The first `terminating` line (starting with `> selfie.c:`) is the inner selfie finishing the test; the second is the outer selfie finishing the inner one. Both must say exit code 0.

```bash
./selfie -c selfie.c -m 64 -c tests/p4_transfer.c -m 2 | grep -c "^ok"
```

`grep -c "^ok"` counts the lines that start with `ok`. Expected: `33`, all of `p4_transfer`'s checks passing one level up.

---

## Step 4 — Review and save the change in Git

```bash
git diff --stat
```

Expected: `1 file changed, 4 insertions(+), 1 deletion(-)` (the reworded comment counts as one line removed and one added).

```bash
git add selfie.c
git commit -m "Phase 6: pipe_destroy wakes readers and writers"
git tag phase-6
git push origin main phase-6
```

- `git add` + `git commit` save the change as a new commit.
- `git tag phase-6` names this commit.
- `git push origin main phase-6` uploads the commit and the tag to GitHub.

Now look at the whole project in one view:

```bash
git diff base phase-6 --stat -- selfie.c
git log --oneline base..phase-6
```

- The first shows everything you changed in `selfie.c` since the course base code (`-- selfie.c` leaves out the guides and other files you added to the repo). Expected: `1 file changed, 482 insertions(+), 32 deletions(-)`.
- The second lists every commit since `base`, newest first: your seven phase commits, plus any others you made in between (like the commits that added the guides).

---

## If you need to start over

```bash
git checkout phase-5 -- selfie.c
```

Brings back `selfie.c` exactly as it was after Phase 5. **This discards your uncommitted Phase 6 edits.**

---

## Worth mentioning in your report

These are known limits of this implementation, not bugs in the tests:

- **No lock is needed:** each syscall handler runs to completion inside the emulator, and the timer only interrupts between program instructions, so checking a pipe and going to sleep is already one atomic step.
- **Retries count as syscalls:** a process that slept repeats its `ecall`, so the context's syscall counter (`ec_syscall`) counts more calls than the program made.
- **Memory is not freed:** destroyed pipes keep their memory (the `TODO` in `implement_pipe_destroy`), because selfie has no `free`.
- **Only `mipster` got the new scheduler:** `hypster` and `mixter` still use the base code's scheduling loop.
- **`wait()` outside the plan:** a child that exits with code 0 before its parent calls `wait()` leaves the parent waiting forever. With Phase 1 selfie reports it as a deadlock instead of crashing, but the real fix belongs in `implement_wait`.
