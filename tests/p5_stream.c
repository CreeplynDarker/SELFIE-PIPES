// child writes 58 bytes into a 16-byte pipe; parent reads 5 bytes at a time
uint64_t main() {
  uint64_t p;
  uint64_t sent;
  uint64_t got;
  uint64_t r;
  uint64_t msg;
  uint64_t len;
  uint64_t* buf;

  msg = (uint64_t) "Pipes carry bytes from one process to another, in order.\n";
  len = 58;

  p = pipe_create();

  if (fork() == 0) {
    sent = 0;
    while (sent < len) {
      r = pipe_write(p, (uint64_t*) (msg + sent), len - sent);
      sent = sent + r;
    }
    exit(0);
  }

  buf = malloc(8);
  got = 0;
  while (got < len) {
    r = pipe_read(p, buf, 5);
    write(1, buf, r);
    got = got + r;
  }

  pipe_destroy(p);

  return 0;
}
