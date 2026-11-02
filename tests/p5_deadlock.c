// a reader with no writer: the scheduler must report a deadlock
uint64_t main() {
  uint64_t p;
  uint64_t* buf;

  p = pipe_create();
  buf = malloc(8);
  pipe_read(p, buf, 1);

  return 0;
}
