// ---- test helpers (paste at the top of every test) ----
uint64_t* out;
uint64_t MINUS1 = 4294967295; // -1 as seen by programs: syscall results are 32-bit

void put(uint64_t c) {
  *out = c;
  write(1, out, 1);
}

void check(uint64_t id, uint64_t pass) {
  // the whole line goes out in ONE write, so lines from different processes never mix
  if (pass) {
    *out = 'o' + 'k' * 256 + ' ' * 65536 + ('0' + id / 10) * 16777216
      + ('0' + id % 10) * 4294967296 + 10 * 1099511627776;
    write(1, out, 6);
  } else {
    *out = 'F' + 'A' * 256 + 'I' * 65536 + 'L' * 16777216 + ' ' * 4294967296
      + ('0' + id / 10) * 1099511627776 + ('0' + id % 10) * 281474976710656 + 10 * 72057594037927936;
    write(1, out, 8);
  }
}

void spin(uint64_t n) {
  // burn CPU so the timer interrupt hands the CPU to another process
  uint64_t i;
  i = 0;
  while (i < n)
    i = i + 1;
}
// ---- end of helpers ----

uint64_t x;

uint64_t main() {
  uint64_t pid;
  uint64_t local;
  uint64_t* heap;

  out = malloc(8);
  heap = malloc(8);

  x = 1;
  local = 42;
  *heap = 7;

  pid = fork();

  if (pid == 0) {
    // child: sees copies of everything, then changes its copies
    check(1, x == 1);
    check(2, local == 42);
    check(3, *heap == 7);
    x = 2;
    local = 43;
    *heap = 9;
    exit(0);
  }

  check(4, pid > 0);
  check(5, pid != MINUS1);

  // let the child run (timer interrupt), then confirm our copies are untouched
  spin(400000);

  check(6, x == 1);
  check(7, local == 42);
  check(8, *heap == 7);

  return 0;
}
