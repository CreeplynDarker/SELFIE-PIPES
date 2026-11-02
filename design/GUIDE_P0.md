# Guide — Phase 0: trustworthy console output

**Goal:** make `write(1, buffer, n)` print exactly `n` bytes.

**Why first:** every test reports its result through `write()`. The base code has a bug that prints extra bytes whenever `n` is not a multiple of 8, so a correct program can look broken. Fix this before anything else.

**What changes:** one place in `selfie.c` (the function `implement_write`). 3 lines are removed and 7 are added.

All commands below run in the **WSL terminal**, inside the project folder. All edits happen in **VS Code** (opened with `code .` from that folder).

---

## Step 0 — Start from a clean, up-to-date copy

```bash
cd ~/SELFIE-PIPES
git status
git pull
```

- `cd ~/SELFIE-PIPES` moves into the project folder.
- `git status` shows whether you have unsaved changes. It should say `nothing to commit, working tree clean`. If it lists modified files, commit or discard them before you start.
- `git pull` downloads any changes from GitHub, so you start from the latest version.

Now confirm the bug is there:

```bash
./run_tests.sh 0
```

`run_tests.sh` builds `selfie.c`, checks the gate (it builds, compiles itself, runs itself), and runs the Phase 0 test. Expected output, before the fix:

```text
== gate
PASS  builds with gcc
PASS  compiles itself without errors or warnings
PASS  runs itself (selfie-on-selfie)
== phase 0
FAIL  p0_write
      expected: abc|0123456789|X  (exit code 0)
      got:      abcdefgh|0123456789|XY  (exit code 0)

1 of 4 checks failed (phases 0..0).
```

The FAIL is the bug: `write(1, "abcdefghij", 3)` printed 8 letters instead of 3.

---

## Step 1 — Edit `implement_write`

**Where:** `selfie.c`, lines 7814–7816, inside `void implement_write(uint64_t* context)`.

In VS Code: open `selfie.c`, press `Ctrl+G`, type `7814` and press Enter. Or press `Ctrl+F` and search for `copy_buffer(context, vbuffer, IO_buffer, size, 0)` (it appears only once).

**Remove these 3 lines** (lines 7814–7816):

```c
  if (copy_buffer(context, vbuffer, IO_buffer, size, 0))
    *(get_regs(context) + REG_A0) = write_to_printf(fd, IO_buffer, size);
  else
```

**Add these 7 lines in their place:**

```c
  if (copy_buffer(context, vbuffer, IO_buffer, size, 0)) {
    // copy_buffer copies whole words and overwrites the terminator set above:
    // terminate again exactly after size bytes
    store_character((char*) IO_buffer, size, 0);

    *(get_regs(context) + REG_A0) = write_to_printf(fd, IO_buffer, size);
  } else
```

**What each change does:**

- `{` after the `if (...)` and `} else` — the `if` branch now has two statements, so it needs braces. The `else` line that follows (`*(get_regs(context) + REG_A0) = sign_shrink(-1, SYSCALL_BITWIDTH);`) stays exactly as it is.
- The two `//` lines are a comment explaining the fix.
- `store_character((char*) IO_buffer, size, 0);` is the fix. Two lines above this spot, the code writes a 0 (end of string) right after the first `size` bytes. But `copy_buffer` copies the user's buffer 8 bytes at a time and overwrites that 0 with whatever came next in memory. `write_to_printf` then prints until it finds a 0, so extra bytes appear. This line writes the 0 again, after the copy, at exactly position `size`.
- `*(get_regs(context) + REG_A0) = write_to_printf(fd, IO_buffer, size);` is the original line, unchanged, now inside the braces.

Indentation (tabs or spaces) does not matter to the compiler. Save the file (`Ctrl+S`).

---

## Step 2 — Test

```bash
./run_tests.sh 0
```

Expected output:

```text
== gate
PASS  builds with gcc
PASS  compiles itself without errors or warnings
PASS  runs itself (selfie-on-selfie)
== phase 0
PASS  p0_write

All 4 checks passed (phases 0..0).
```

If it fails instead:

- `FAIL builds with gcc` — gcc printed an error above the result; usually a missing `{`, `}` or `;`. Compare your lines with Step 1.
- `FAIL compiles itself` — selfie's own compiler is stricter than gcc. The lines printed under the FAIL say which line it rejected.
- `FAIL p0_write` with the old output — the file was not saved, or the change is in a different function.

---

## Step 3 — Review and save the change in Git

```bash
git diff
```

Shows exactly what you changed, compared with the last commit: lines starting with `-` were removed, lines with `+` were added. Press `q` to leave the view. You should see only the change from Step 1.

```bash
git diff --stat
```

A one-line summary. Expected: `selfie.c | 8 ++++++--` and `1 file changed, 6 insertions(+), 2 deletions(-)`. (Git counts 6 added and 2 removed because the `if` line and the `else` line changed, while the `write_to_printf` line moved without changing.)

```bash
git add selfie.c
git commit -m "Phase 0: write() prints exactly size bytes"
```

- `git add selfie.c` marks the file to be included in the next commit.
- `git commit -m "..."` saves a snapshot of the marked files with that message.

```bash
git tag phase-0
git push origin main phase-0
```

- `git tag phase-0` puts the name `phase-0` on this commit, so you can always come back to it or compare against it.
- `git push origin main phase-0` uploads the commit (on branch `main`) and the tag to GitHub.

Check: `git diff base phase-0 --stat` should show the same one-file summary as above.

---

## If you need to start over

```bash
git checkout base -- selfie.c
```

Replaces your `selfie.c` with the version tagged `base` (the untouched course code). **This discards your uncommitted edits to `selfie.c`.** Committed work is never lost: it stays in Git's history.

---

**Next:** `GUIDE_P1.md` — repair blocking and scheduling in the base code.
