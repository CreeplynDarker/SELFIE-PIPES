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

// a reader blocked on an empty pipe gets -1 when the pipe is destroyed
uint64_t main() {
  uint64_t p;
  uint64_t* buf;

  out = malloc(8);
  buf = malloc(8);
  p = pipe_create();

  if (fork() == 0) {
    check(2, pipe_read(p, buf, 4) == MINUS1);
    exit(0);
  }

  spin(400000);
  check(1, pipe_destroy(p) == 0);
  spin(400000);
  check(3, pipe_destroy(p) == MINUS1);

  return 0;
}
