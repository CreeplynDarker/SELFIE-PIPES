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

// single process, never touches an empty or full pipe in a way that would block
uint64_t main() {
  uint64_t p;
  uint64_t* buf;
  uint64_t* big;
  uint64_t msg;

  out = malloc(8);
  buf = malloc(8);

  p = pipe_create();

  check(1, pipe_write(p, "ABCDEFGHIJ", 10) == 10);
  *buf = 0;
  check(2, pipe_read(p, buf, 4) == 4);
  check(3, *buf == 'A' + 'B' * 256 + 'C' * 65536 + 'D' * 16777216);
  *buf = 0;
  check(4, pipe_read(p, buf, 100) == 6);
  check(5, *buf == 'E' + 'F' * 256 + 'G' * 65536 + 'H' * 16777216 + 'I' * 4294967296 + 'J' * 1099511627776);

  // short write: only 16 bytes fit
  msg = (uint64_t) "abcdefghijklmnopqrstuvwxyz0123456789ABCD";
  check(6, pipe_write(p, (uint64_t*) msg, 40) == 16);
  check(7, pipe_read(p, buf, 8) == 8);
  check(8, *buf == *((uint64_t*) msg));
  check(9, pipe_read(p, buf, 8) == 8);
  check(10, *buf == *((uint64_t*) msg + 1));

  // unaligned source
  check(11, pipe_write(p, (uint64_t*) (msg + 3), 2) == 2);
  *buf = 0;
  check(12, pipe_read(p, buf, 2) == 2);
  check(13, *buf == 'd' + 'e' * 256);

  // a 1-byte read must not touch the other bytes of the word
  check(14, pipe_write(p, "Q", 1) == 1);
  *buf = 'x' + 'y' * 256 + 'z' * 65536;
  check(15, pipe_read(p, buf, 1) == 1);
  check(16, *buf == 'Q' + 'y' * 256 + 'z' * 65536);

  // unaligned destination
  check(17, pipe_write(p, "RS", 2) == 2);
  *buf = 0;
  check(18, pipe_read(p, (uint64_t*) ((uint64_t) buf + 5), 2) == 2);
  check(19, *buf == 'R' * 1099511627776 + 'S' * 281474976710656);

  // destination on a heap page the program never touched
  big = malloc(16384);
  check(20, pipe_write(p, "Z", 1) == 1);
  check(21, pipe_read(p, big + 1024, 1) == 1);
  check(22, *(big + 1024) == 'Z');

  // bad address: error, and the byte stays in the pipe
  check(23, pipe_write(p, (uint64_t*) 8, 1) == MINUS1);
  check(24, pipe_write(p, "W", 1) == 1);
  check(25, pipe_read(p, (uint64_t*) 8, 1) == MINUS1);
  *buf = 0;
  check(26, pipe_read(p, buf, 1) == 1);
  check(27, *buf == 'W');

  // zero sizes and bad IDs
  check(28, pipe_write(p, buf, 0) == 0);
  check(29, pipe_read(p, buf, 0) == 0);
  check(30, pipe_write(99, buf, 1) == MINUS1);
  check(31, pipe_read(99, buf, 1) == MINUS1);

  check(32, pipe_destroy(p) == 0);
  check(33, pipe_write(p, buf, 1) == MINUS1);

  return 0;
}
