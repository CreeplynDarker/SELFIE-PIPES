# Guide — Phase 2: verify and repair the given fork()

**Goal:** the base code's `fork()` gives the child a private copy of the parent's memory, even when the parent has heap memory it reserved with `malloc` but never touched.

**Why:** pipes connect processes, and `fork()` is how a test creates the second process. The course base code already implements `fork()` (syscall 215, `implement_fork`), so this phase does **not** rewrite it. It fixes the one bug found: copying memory crashes selfie after a large, partly used `malloc`.

**What changes:** two loops inside `implement_fork` in `selfie.c`. In each loop, one `if` line is added and the copy line moves under it (4 lines become 5). **Do Phases 0 and 1 first**; the line numbers below assume they are done and that you do Step 1 before Step 2.

All commands run in the **WSL terminal** inside `~/SELFIE-PIPES`; all edits happen in **VS Code**.

---

## Step 0 — Start from Phase 1 and see the bug

```bash
cd ~/SELFIE-PIPES
git status
git log --oneline -3
```

- `git status` should say `nothing to commit, working tree clean`. If it lists changed files, commit or discard them first.
- `git log --oneline -3` lists the last 3 commits, newest first. The top one should be your Phase 1 commit (`Phase 1: repair blocking lists and scheduler, report deadlocks`).

Now the **negative check**, to see the bug before fixing it:

```bash
./run_tests.sh 2
```

Expected output, before the change:

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
FAIL  p2_fork_gap
      expected: ok 01 / ok 02  (exit code 0)
      got:      <no output>  (exit code none (selfie crashed, shell status 139))

1 of 10 checks failed (phases 0..2).
```

- `p2_fork` and `p2_exit` already pass: the given `fork()` works for small programs.
- `p2_fork_gap` crashes selfie itself (status 139 = segmentation fault). The test does `malloc(65536)`, writes only near the end of that block, then calls `fork()`.

**Why it crashes:** `implement_fork` copies the parent's memory one 8-byte word at a time, from the lowest to the highest page in use. With a big `malloc`, some pages in that range were reserved but never touched, so they were never given real memory ("mapped"). Reading such a page with `load_virtual_memory` makes selfie read from an invalid host address and crash.

---

## Step 1 — Copy of the low pages: skip words that aren't mapped

**Where:** inside `void implement_fork(uint64_t* context)`, under the comment `/* Copy low pages */`. Search for `get_highest_lo_page(context)) {`.

**Remove these 4 lines** (lines 8089–8092):

```c
	while (get_page_of_virtual_address(vaddr) < get_highest_lo_page(context)) {
		map_and_store (child, vaddr, load_virtual_memory(get_pt(context), vaddr));
		vaddr = vaddr + WORDSIZE;
	}
```

**Add these 5 lines in their place**:

```c
	while (get_page_of_virtual_address(vaddr) < get_highest_lo_page(context)) {
		if (is_virtual_address_mapped(get_pt(context), vaddr))
			map_and_store(child, vaddr, load_virtual_memory(get_pt(context), vaddr));
		vaddr = vaddr + WORDSIZE;
	}
```

**What changed:**

- `if (is_virtual_address_mapped(get_pt(context), vaddr))` copies a word only if the parent's page that holds it is actually mapped. `get_pt(context)` is the parent's page table.
- The copy line itself is the same as before, just indented under the `if`. (The space in `map_and_store (child, ...` was removed; that is only style.)
- Skipping is safe: the parent never wrote to an unmapped page, so it contains nothing to copy. If the child touches that page later, selfie's normal page-fault handling gives it a fresh, zeroed page, exactly as it would for the parent.

---

## Step 2 — Copy of the high pages (the stack): the same fix

**Where:** a few lines below, under `/* Copy high pages */`. Search for `get_highest_hi_page(context)) {`.

**Remove these 4 lines** (lines 8098–8101):

```c
	while (get_page_of_virtual_address(vaddr) < get_highest_hi_page(context)) {
		map_and_store (child, vaddr, load_virtual_memory(get_pt(context), vaddr));
		vaddr = vaddr + WORDSIZE;
	}
```

**Add these 5 lines in their place**:

```c
	while (get_page_of_virtual_address(vaddr) < get_highest_hi_page(context)) {
		if (is_virtual_address_mapped(get_pt(context), vaddr))
			map_and_store(child, vaddr, load_virtual_memory(get_pt(context), vaddr));
		vaddr = vaddr + WORDSIZE;
	}
```

**Why here too:** this loop copies the stack region the same way. It hasn't crashed in the tests, but it has the same weakness, and the plan requires both loops to be safe (R2.2).

Save the file (`Ctrl+S`).

---

## Step 3 — Test

```bash
./run_tests.sh 2
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
== phase 2
PASS  p2_fork
PASS  p2_exit
PASS  p2_fork_gap

All 10 checks passed (phases 0..2).
```

What the Phase 2 tests prove:

- `p2_fork` prints `ok 01`, `ok 04`, `ok 02`, `ok 05`, `ok 03`, `ok 06`, `ok 07`, `ok 08`. Lines 01–03 come from the child, which sees copies of the parent's global, local and heap variables. Lines 04–05 are the parent checking it got a valid PID. Lines 06–08 confirm the child's changes did not reach the parent's memory. Child and parent alternate because this base switches process after every syscall.
- `p2_exit` ends with exit code 5: the parent exits first, and the machine keeps running the child until it exits too.
- `p2_fork_gap` prints `ok 01` and `ok 02` instead of crashing: the child still sees the value the parent wrote, and its own change doesn't reach the parent.

If something fails: `p2_fork_gap` still crashing means one of the two `if` lines is missing or in the wrong loop; a build or self-compile FAIL usually means a missing `;` or `)`.

---

## Step 4 — Review and save the change in Git

```bash
git diff --stat
```

Expected: `selfie.c | 6 ++++--` and `1 file changed, 4 insertions(+), 2 deletions(-)`. (Per loop, Git sees the old copy line removed, and the `if` line plus the re-indented copy line added.)

```bash
git diff
```

Shows the two changes, one per loop (`q` to exit).

```bash
git add selfie.c
git commit -m "Phase 2: fork skips unmapped pages when copying memory"
git tag phase-2
git push origin main phase-2
```

- `git add` + `git commit` save the change as a new commit.
- `git tag phase-2` names this commit, so `git diff phase-1 phase-2` shows exactly this phase.
- `git push origin main phase-2` uploads the commit and the tag to GitHub.

---

## If you need to start over

```bash
git checkout phase-1 -- selfie.c
```

Brings back `selfie.c` exactly as it was after Phase 1. **This discards your uncommitted Phase 2 edits.**

---

**Next:** `GUIDE_P3.md` — pipe objects and the global registry.
