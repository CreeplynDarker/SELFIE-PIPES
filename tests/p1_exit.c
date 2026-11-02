uint64_t main() {
  uint64_t i;
  i = 0;
  // long enough for many timer interrupts with a single context
  while (i < 2000000)
    i = i + 1;
  write(1, "survived timer interrupts\n", 26);
  return 7;
}
