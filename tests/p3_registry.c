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

uint64_t main() {
  uint64_t p1;
  uint64_t p2;
  uint64_t i;

  out = malloc(8);

  p1 = pipe_create();
  p2 = pipe_create();
  check(1, p1 == 1);
  check(2, p2 == 2);
  check(3, pipe_destroy(p1) == 0);
  check(4, pipe_destroy(p1) == MINUS1);
  check(5, pipe_create() == 1);
  check(6, pipe_destroy(0) == MINUS1);
  check(7, pipe_destroy(99) == MINUS1);

  // IDs 3 .. 15 are still free
  i = 0;
  while (i < 13) {
    pipe_create();
    i = i + 1;
  }
  check(8, pipe_create() == MINUS1);

  return 0;
}
