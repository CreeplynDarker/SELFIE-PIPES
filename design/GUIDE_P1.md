# Guide — Phase 1: repair blocking and scheduling in the base code

**Goal:** the scheduler only ever runs processes that are READY or RUNNING, two processes can be blocked at the same time, and when every remaining process is blocked selfie reports a deadlock (exit code 28) instead of crashing.

**Why:** pipes will put processes to sleep with the base code's `block_context()` and wake them with `unblock_context()`. That code, and the scheduler in `mipster()`, have bugs that crash selfie as soon as processes block. Without this phase, the pipe tests in Phase 5 hang or crash.

**What changes:** six places in `selfie.c`. **Do Phase 0 first**, and do the steps below **in order, from top to bottom**: each line number already accounts for the lines added by Phase 0 and by the earlier steps of this guide. If a line number doesn't match, search for the text given in the step (`Ctrl+F`); the search text is reliable even when line numbers drift.

| Step | Function or place | Requirement in the plan | Lines removed → added |
| --- | --- | --- | --- |
| 1 | exit codes | R1.7 | 1 → 2 |
| 2 | `implement_exit` | R1.5 | 1 → 1 |
| 3 | `init_context` | R1.4 | 3 → 6 |
| 4 | `block_context` | R1.2 | 8 → 8 |
| 5 | `unblock_context` | R1.3 | 8 → 11 |
| 6 | `mipster` (scheduler) | R1.6, R1.7, R1.8 | 32 → 54 |

All commands run in the **WSL terminal** inside `~/SELFIE-PIPES`; all edits happen in **VS Code**.

A note on the code style: selfie.c must compile itself, and selfie's compiler (C\*) has no `&&`, `||`, `for`, `break` or `struct`. That is why the new code uses nested `if`s instead of `&&`.

---

## Step 0 — Start from Phase 0 and see the bugs

```bash
cd ~/SELFIE-PIPES
git status
git log --oneline -3
```

- `git status` should say `nothing to commit, working tree clean`.
- `git log --oneline -3` lists the last 3 commits, newest first. The top one should be your Phase 0 commit (`Phase 0: write() prints exactly size bytes`).

Now run the tests up to Phase 1. This is the **negative check**: it proves the Phase 1 tests can detect the bugs you're about to fix.

```bash
./run_tests.sh 1
```

Expected output, before the changes:

```text
== gate
PASS  builds with gcc
PASS  compiles itself without errors or warnings
PASS  runs itself (selfie-on-selfie)
== phase 0
PASS  p0_write
== phase 1
PASS  p1_exit
FAIL  p1_deadlock
      expected: before wait / deadlock: 1 context(s) blocked and none ready  (exit code 28)
      got:      <no output>  (exit code none (selfie crashed, shell status 139))
FAIL  p1_two_waiters
      expected: ok 01 / ok 02 / ok 03  (exit code 0)
      got:      <no output>  (exit code none (selfie crashed, shell status 139))

2 of 7 checks failed (phases 0..1).
```

"Shell status 139" means selfie itself crashed with a segmentation fault. `p1_deadlock` calls `wait()` with no child, so nobody can ever wake it; `p1_two_waiters` has two processes blocked in `wait()` at the same time.

---

## Step 1 — Add an exit code for deadlocks

**Where:** line 2492. Search for `EXITCODE_UNCAUGHTEXCEPTION      = 27`.

**Remove this line:**

```c
uint64_t EXITCODE_UNCAUGHTEXCEPTION      = 27;
```

**Add these 2 lines in its place** (the first one is the same line, kept):

```c
uint64_t EXITCODE_UNCAUGHTEXCEPTION      = 27;
uint64_t EXITCODE_DEADLOCK                = 28;
```

**Why:** when every process is blocked, selfie will stop with exit code 28, so the tests can tell a deadlock apart from a normal exit. 28 is the next free number after the existing exit codes.

---

## Step 2 — `implement_exit`: write the exit status safely

**Where:** line 7605, inside `void implement_exit(uint64_t* context)`. Search for `store_virtual_memory(get_pt(parent), wstatus_addr`.

**Remove this line:**

```c
				store_virtual_memory(get_pt(parent), wstatus_addr, get_exit_code(context));
```

**Add this line in its place:**

```c
				map_and_store(parent, wstatus_addr, get_exit_code(context));
```

**Why:** when a child exits and its parent is blocked in `wait()`, the kernel writes the child's exit code into the parent's status variable. `store_virtual_memory` assumes that memory page is already mapped; if the parent never touched it, selfie crashes. `map_and_store` maps the page first when needed, then stores. This is the crash behind `p1_two_waiters`.

---

## Step 3 — `init_context`: start the process fields at 0

**Where:** lines 11171–11173, the last lines of `void init_context(...)`. Search for `set_nchildren(context, 0);`.

**Remove these 3 lines:**

```c
  set_status(context, STATUS_READY);
  set_nchildren(context, 0);
}
```

**Add these 6 lines in their place:**

```c
  set_status(context, STATUS_READY);
  set_nchildren(context, 0);
  set_parent_process(context, (uint64_t*) 0);
  set_child_exit_code(context, 0);
  set_child_pid(context, 0);
}
```

**Why:** the three new lines reset the parent pointer, the last child's exit code and the last child's PID. When a process exits, its context goes to a free list and is reused by the next `fork()`. `smalloc` doesn't clear memory, so without these lines a new process could start with a stale parent or a stale child exit code, and `wait()` could return an old result.

---

## Step 4 — `block_context`: the blocked process becomes the head of the list

**Where:** lines 11231–11238, inside `void block_context(uint64_t* context)`. Search for `set_next_context(context, blocked_contexts);`; the block to replace starts at that line and ends at the `}` that closes the `else`.

**Remove these 8 lines** (two of the lines that look empty contain only tabs; select from the first line to the closing `}` and delete them all):

```c
	set_next_context(context, blocked_contexts);
	
	if (blocked_contexts != (uint64_t*) 0) {
		
		set_prev_context(blocked_contexts, context);
	} else {
		blocked_contexts = context;
	}
```

**Add these 8 lines in their place:**

```c
	set_next_context(context, blocked_contexts);
	set_prev_context(context, (uint64_t*) 0);

	if (blocked_contexts != (uint64_t*) 0)
		set_prev_context(blocked_contexts, context);

	// the newly blocked context is always the new head of the list
	blocked_contexts = context;
```

**Why:** `blocked_contexts` is a linked list of the blocked processes, and the code above puts the new one in front of the old head. The original only updated the head when the list was empty. With one process already blocked, the second one was linked in but the list's head still pointed at the first, so the second was effectively lost, and the list broke when processes were unblocked. Now the new process always becomes the head, and its `prev` pointer is set to 0 because nothing is in front of it.

---

## Step 5 — `unblock_context`: keep both lists valid

**Where:** lines 11253–11260, inside `void unblock_context(uint64_t* context)`. Search for `void unblock_context(uint64_t* context) {` to reach the function. The block to replace starts at the line `if (get_prev_context(context) != (uint64_t*) 0) {` inside it (the only one there) and goes down to `used_contexts = context;`, just before the line `set_status(context, STATUS_READY);`.

**Remove these 8 lines:**

```c
	if (get_prev_context(context) != (uint64_t*) 0) {
		set_next_context(get_prev_context(context), get_next_context(context));
		set_prev_context(context, (uint64_t*) 0);
	}

	set_prev_context(used_contexts, context);
	set_next_context(context, used_contexts);
	used_contexts = context;
```

**Add these 11 lines in their place:**

```c
	if (get_prev_context(context) != (uint64_t*) 0) {
		set_next_context(get_prev_context(context), get_next_context(context));
		set_prev_context(context, (uint64_t*) 0);
	} else
		// context was the head of the blocked list
		blocked_contexts = get_next_context(context);

	if (used_contexts != (uint64_t*) 0)
		set_prev_context(used_contexts, context);
	set_next_context(context, used_contexts);
	used_contexts = context;
```

**What changed:**

- `} else` + `blocked_contexts = get_next_context(context);` — if the process being unblocked was the head of the blocked list (nothing in front of it), the head must move to the next one. The original left the head pointing at a process that was no longer blocked.
- `if (used_contexts != (uint64_t*) 0)` — the process goes back to the front of `used_contexts` (the runnable processes). The original always set the old front's `prev` pointer, which crashes when the runnable list is empty.

---

## Step 6 — `mipster`: a scheduler that never picks a blocked process

**Where:** lines 11891–11922: the whole function `uint64_t mipster(uint64_t* to_context)`. Search for `uint64_t mipster(uint64_t* to_context) {`. The function ends at the `}` on the line just before `uint64_t hypster(uint64_t* to_context) {`.

**Remove the whole function, these 32 lines:**

```c
uint64_t mipster(uint64_t* to_context) {
  uint64_t timeout;
  uint64_t* from_context;

  timeout = TIMESLICE;

  while (1) {
    from_context = mipster_switch(to_context, timeout);

    if (get_parent(from_context) != MY_CONTEXT) {
      // switch to parent which is in charge of handling exceptions
      to_context = get_parent(from_context);

      timeout = TIMEROFF;
    } else if (handle_exception(from_context) == EXIT)
      return get_exit_code(from_context);
    else {
      // TODO: scheduler should go here
	  if (get_next_context(to_context) == (uint64_t *) 0) {
		to_context = used_contexts;
	  } else if (get_status(to_context) == STATUS_FREED) {
		to_context = used_contexts;
	  } else {
		to_context = get_next_context (from_context);
	  }

	  //to_context = from_context;
      timeout = TIMESLICE;
	  set_status(to_context, STATUS_RUNNING);
    }
  }
}
```

**Add these 54 lines in its place** (two new functions, then the new `mipster`):

```c
uint64_t* next_ready_context(uint64_t* from_context) {
  // round robin over used_contexts, which holds only READY and RUNNING contexts
  if (get_status(from_context) == STATUS_RUNNING)
    set_status(from_context, STATUS_READY);

  if (get_status(from_context) == STATUS_READY)
    // from_context is still in used_contexts: take its successor
    if (get_next_context(from_context) != (uint64_t*) 0)
      return get_next_context(from_context);

  // wrap around, or from_context was blocked or freed: its next pointer
  // now belongs to another list, so start over at the head (0 = none ready)
  return used_contexts;
}

uint64_t report_deadlock() {
  printf("%s: deadlock: %lu context(s) blocked and none ready\n", selfie_name, N_BLOCKED_CONTEXTS);

  return EXITCODE_DEADLOCK;
}

uint64_t mipster(uint64_t* to_context) {
  uint64_t timeout;
  uint64_t* from_context;

  timeout = TIMESLICE;

  while (1) {
    from_context = mipster_switch(to_context, timeout);

    if (get_parent(from_context) != MY_CONTEXT) {
      // switch to parent which is in charge of handling exceptions
      to_context = get_parent(from_context);

      timeout = TIMEROFF;
    } else if (handle_exception(from_context) == EXIT) {
      // the last running context exited while others still sleep
      if (used_contexts == (uint64_t*) 0)
        if (N_BLOCKED_CONTEXTS > 0)
          return report_deadlock();

      return get_exit_code(from_context);
    } else {
      to_context = next_ready_context(from_context);

      if (to_context == (uint64_t*) 0)
        // every remaining context is blocked
        return report_deadlock();

      timeout = TIMESLICE;
      set_status(to_context, STATUS_RUNNING);
    }
  }
}
```

**What each part does:**

- **`next_ready_context(from_context)`** picks the next process to run, round robin. `used_contexts` holds only the processes that can run (READY or RUNNING); blocked ones live in `blocked_contexts`, and exited ones in `free_contexts`.
  - If the process that just ran is still RUNNING, it becomes READY.
  - If it is READY (still in `used_contexts`) and has a successor, the successor runs next.
  - Otherwise, start again at the head of `used_contexts`. This covers the end of the list, and also a process that just blocked or exited: its `next` pointer now belongs to the blocked or free list, so following it (as the old code did) could run a blocked or freed process.
  - If `used_contexts` is empty, it returns 0: nobody can run.
- **`report_deadlock()`** prints `deadlock: N context(s) blocked and none ready` and returns exit code 28 (Step 1). `N_BLOCKED_CONTEXTS` is the base code's counter of blocked processes.
- **In `mipster()`:**
  - The `EXIT` branch: when a process exits and no runnable process is left, the old code just returned. Now, if processes are still blocked, that's a deadlock (nobody will ever wake them), so it reports it. The nested `if`s replace `&&`, which C\* doesn't have.
  - The `else` branch: the old code's `TODO: scheduler should go here` block is replaced by a call to `next_ready_context`. If it returns 0, every remaining process is blocked: report the deadlock instead of crashing on a null context (the crash behind `p1_deadlock`).
  - `set_status(to_context, STATUS_RUNNING)` stays, as in the base code.
  - The base's policy is kept: after every exception (every syscall, timer interrupt or page fault), the scheduler moves on to the next process.

Save the file (`Ctrl+S`).

---

## Step 7 — Test

```bash
./run_tests.sh 1
```

Expected output:

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

All 7 checks passed (phases 0..1).
```

What the Phase 1 tests prove:

- `p1_deadlock` prints `before wait`, then selfie reports `deadlock: 1 context(s) blocked and none ready` with exit code 28 (Steps 1 and 6).
- `p1_two_waiters` prints `ok 01`, `ok 02`, `ok 03`: two processes were blocked at once and both woke up correctly (Steps 2–5).
- `p1_exit` still ends with exit code 7: a normal program behaves exactly as before.

To run a single test while you debug: `./run_tests.sh 1 p1_deadlock`.

If something fails:

- `FAIL builds with gcc` or `FAIL compiles itself` — a typo in one of the steps; the lines under the FAIL point to it. The most common one: a missing `}` when replacing `mipster`, or `mipster` replaced only partly.
- `p1_deadlock` still crashes (status 139) — check Step 6, especially the `if (to_context == (uint64_t*) 0)` check.
- `p1_two_waiters` crashes — check Step 2 (`map_and_store`) first, then Steps 4 and 5.
- A test runs for minutes — a list is broken and the scheduler loops; recheck Steps 4 and 5.

---

## Step 8 — Review and save the change in Git

```bash
git diff --stat
```

Expected: `selfie.c | 71 ++++++++++++++++++++++++++++++++++++++++++++-------------------` and `1 file changed, 50 insertions(+), 21 deletions(-)`.

```bash
git diff
```

Read through every change (`q` to exit). Every change should belong to one of Steps 1–6; Git may show the `mipster` change as two separate blocks.

```bash
git add selfie.c
git commit -m "Phase 1: repair blocking lists and scheduler, report deadlocks"
git tag phase-1
git push origin main phase-1
```

- `git add` + `git commit` save the change as a new commit.
- `git tag phase-1` names this commit.
- `git push origin main phase-1` uploads the commit and the tag to GitHub.

Useful comparisons from now on:

```bash
git diff phase-0 phase-1 --stat   # only what Phase 1 changed
git diff base phase-1 --stat      # everything since the course base code
```

---

## If you need to start over

```bash
git checkout phase-0 -- selfie.c
```

Brings back `selfie.c` exactly as it was after Phase 0. **This discards your uncommitted Phase 1 edits.**

---

**Known issue outside this phase:** `wait()` treats a child exit code of 0 as "no child has exited yet". A child that exits with 0 before its parent calls `wait()` leaves the parent blocked forever. After Phase 1, selfie reports that as a deadlock instead of crashing, but the real fix belongs in `implement_wait`, which is outside the pipe plan.

**Next:** Phase 2 — verify and repair the given `fork()` (see the plan in `design/`).
