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

uint64_t main() {
  out = malloc(8);

  if (fork() == 0) {
    // child outlives the parent
    spin(400000);
    check(2, 1);
    exit(5);
  }

  check(1, 1);
  return 0;
}
