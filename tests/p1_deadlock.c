// wait() with no child: nobody can ever wake this process
uint64_t main() {
  uint64_t* st;

  st = malloc(8);
  write(1, "before wait\n", 12);
  wait(st);
  write(1, "after wait\n", 11);

  return 0;
}
