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

// two readers sleep on one pipe; each byte wakes both, only one gets it, the other sleeps again
uint64_t p;

void reader(uint64_t id) {
  uint64_t* buf;

  buf = malloc(8);
  *buf = 0;

  pipe_read(p, buf, 1);

  // "rN=X" plus newline in one write
  *out = 'r' + ('0' + id) * 256 + '=' * 65536 + *buf * 16777216 + 10 * 4294967296;
  write(1, out, 5);

  exit(0);
}

uint64_t main() {
  out = malloc(8);

  p = pipe_create();

  if (fork() == 0)
    reader(1);
  if (fork() == 0)
    reader(2);

  spin(400000);
  pipe_write(p, "A", 1);
  spin(400000);
  pipe_write(p, "B", 1);
  spin(400000);

  return 0;
}
