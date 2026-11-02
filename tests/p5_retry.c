// ---- test helpers (paste at the top of every test) ----
uint64_t* out;
uint64_t MINUS1 = 4294967295; // -1 as seen by programs: syscall results are 32-bit

void put(uint64_t c) {
  *out = c;
  write(1, out, 1);
}

void check(uint64_t id, uint64_t pass) {
  if (pass) {
    put('o'); put('k');
  } else {
    put('F'); put('A'); put('I'); put('L');
  }
  put(' '); put('0' + id / 10); put('0' + id % 10); put(10);
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

  put('r'); put('0' + id); put('='); put(*buf); put(10);

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
