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
