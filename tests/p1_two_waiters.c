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

// two contexts blocked at the same time (parent and child both in wait)
uint64_t main() {
  uint64_t* st;

  out = malloc(8);
  st = malloc(8);

  if (fork() == 0) {
    if (fork() == 0) {
      spin(600000);
      check(1, 1);   // grandchild finishes first
      exit(3);
    }
    wait(st);
    check(2, 1);     // child wakes after the grandchild exits
    exit(4);
  }

  wait(st);
  check(3, 1);       // parent wakes last
  return 0;
}
