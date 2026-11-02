#!/usr/bin/env bash
# run_tests.sh — check selfie.c against the phase gates of the pipe plan
#
# usage: ./run_tests.sh [PHASE] [TEST]
#   PHASE  0..6 (default 6): run the gate and every test of phases 0..PHASE
#   TEST   optional test name (e.g. p3_registry) to run only that test
#
# For each phase the gate is: selfie.c builds with gcc, compiles itself with
# no "error" or "warning" line, and still runs itself (selfie-on-selfie).
# Each test passes when its program output (between selfie's >>>> and <<<<
# lines, blank lines dropped) and its exit code match the expected values
# from the plan. Exit status: 0 if everything passed, 1 otherwise.

set -u
cd "$(dirname "$0")"

PHASE="${1:-6}"
ONLY="${2:-}"

if ! [[ "$PHASE" =~ ^[0-6]$ ]]; then
  echo "usage: $0 [PHASE 0-6] [TEST]" >&2
  exit 2
fi

if [ -t 1 ]; then GREEN=$'\e[32m'; RED=$'\e[31m'; DIM=$'\e[2m'; OFF=$'\e[0m'; else GREEN=; RED=; DIM=; OFF=; fi

# run a command with a time limit when `timeout` exists (it does on Linux and WSL)
if command -v timeout >/dev/null 2>&1; then LIMIT=(timeout 300); else LIMIT=(); fi

passed=0
failed=0

pass() { echo "${GREEN}PASS${OFF}  $1"; passed=$((passed + 1)); }
fail() { echo "${RED}FAIL${OFF}  $1"; failed=$((failed + 1)); }

# ---------------------------------------------------------------- gate

if [ -z "$ONLY" ]; then
  echo "== gate"
fi

if gcc -w -O2 -m64 -D'uint64_t=unsigned long' selfie.c -o selfie; then
  [ -z "$ONLY" ] && pass "builds with gcc"
else
  fail "builds with gcc"
  echo "Cannot continue without a selfie binary."
  exit 1
fi

if [ -z "$ONLY" ]; then
  out=$("${LIMIT[@]}" ./selfie -c selfie.c 2>&1)
  if echo "$out" | grep -i -E "error|warning" >/dev/null; then
    fail "compiles itself without errors or warnings"
    echo "$out" | grep -i -E "error|warning" | head -5 | sed "s/^/      ${DIM}/;s/$/${OFF}/"
  else
    pass "compiles itself without errors or warnings"
  fi

  out=$("${LIMIT[@]}" ./selfie -c selfie.c -m 64 -c hello.c -m 1 2>&1)
  hits=$(echo "$out" | grep -c -E "^hello from selfie|terminating .* with exit code 0")
  if [ "$hits" -eq 3 ]; then
    pass "runs itself (selfie-on-selfie)"
  else
    fail "runs itself (selfie-on-selfie)"
  fi
fi

# ---------------------------------------------------------------- tests

# expected program output (lines joined with " / ") and exit code, measured
# on a build of the plan; see design/Selfie Pipes Phased Implementation Plan.md
DEADLOCK="deadlock: 1 context(s) blocked and none ready"
OK3="ok 01 / ok 02 / ok 03"
OK8="ok 01 / ok 02 / ok 03 / ok 04 / ok 05 / ok 06 / ok 07 / ok 08"
OK33="ok 01 / ok 02 / ok 03 / ok 04 / ok 05 / ok 06 / ok 07 / ok 08 / ok 09 / ok 10 / ok 11 / ok 12 / ok 13 / ok 14 / ok 15 / ok 16 / ok 17 / ok 18 / ok 19 / ok 20 / ok 21 / ok 22 / ok 23 / ok 24 / ok 25 / ok 26 / ok 27 / ok 28 / ok 29 / ok 30 / ok 31 / ok 32 / ok 33"

# test name | first phase | expected output | expected exit code
TESTS=(
  "p0_write|0|abc|0123456789|X|0"
  "p1_exit|1|survived timer interrupts|7"
  "p1_deadlock|1|before wait / $DEADLOCK|28"
  "p1_two_waiters|1|$OK3|0"
  "p2_fork|2|ok 01 / ok 04 / ok 02 / ok 05 / ok 03 / ok 06 / ok 07 / ok 08|0"
  "p2_exit|2|ok 01 / ok 02|5"
  "p2_fork_gap|2|ok 01 / ok 02|0"
  "p3_registry|3|$OK8|0"
  "p4_transfer|4|$OK33|0"
  "p4_temporary|4|$OK3|0"
  "p5_stream|5|Pipes carry bytes from one process to another, in order.|0"
  "p5_retry|5|r2=A / r1=B|0"
  "p5_deadlock|5|$DEADLOCK|28"
  "p6_destroy_reader|6|$OK3|0"
  "p6_destroy_writer|6|ok 01 / ok 03 / ok 02|0"
)

current=-1

for entry in "${TESTS[@]}"; do
  name="${entry%%|*}"
  rest="${entry#*|}"
  first="${rest%%|*}"
  rest="${rest#*|}"
  want_code="${rest##*|}"
  want_out="${rest%|*}"

  # p4_temporary is a phase-4-only test: from phase 5 on, an empty read blocks
  if [ "$name" = "p4_temporary" ] && [ "$PHASE" -ge 5 ]; then
    want_out="$DEADLOCK"
    want_code=28
  fi

  [ "$first" -le "$PHASE" ] || continue
  [ -z "$ONLY" ] || [ "$name" = "$ONLY" ] || continue

  if [ "$first" != "$current" ]; then
    echo "== phase $first"
    current="$first"
  fi

  if [ ! -f "tests/$name.c" ]; then
    fail "$name (tests/$name.c not found)"
    continue
  fi

  raw=$("${LIMIT[@]}" ./selfie -c "tests/$name.c" -m 2 2>&1)
  status=$?

  got_out=$(echo "$raw" | sed -n '/>>>>>>>>/,/<<<<<<<</p' | grep -v '>>>>>>>>\|<<<<<<<<' \
    | sed 's#^\./selfie: ##' | grep -v '^[[:space:]]*$' \
    | awk 'NR > 1 { printf " / " } { printf "%s", $0 }')
  got_code=$(echo "$raw" | grep "terminating .* with exit code" | tail -1 | sed 's/.*exit code //')

  if [ -z "$got_code" ]; then
    if [ "$status" -eq 124 ]; then
      got_code="none (timed out after 300 s)"
    else
      got_code="none (selfie crashed, shell status $status)"
    fi
  fi

  if [ "$got_out" = "$want_out" ] && [ "$got_code" = "$want_code" ]; then
    pass "$name"
  else
    fail "$name"
    echo "      expected: ${want_out:-<no output>}  (exit code $want_code)"
    echo "      got:      ${got_out:-<no output>}  (exit code $got_code)"
    echo "$raw" | grep -E "syntax error|undeclared|uninitialized|exception" | head -3 \
      | sed "s/^/      ${DIM}/;s/$/${OFF}/"
  fi
done

echo
if [ "$failed" -eq 0 ]; then
  echo "${GREEN}All $passed checks passed${OFF} (phases 0..$PHASE)."
  exit 0
else
  echo "${RED}$failed of $((passed + failed)) checks failed${OFF} (phases 0..$PHASE)."
  exit 1
fi
