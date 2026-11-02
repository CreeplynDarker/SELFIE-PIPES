uint64_t main() {
  write(1, "abcdefghij", 3);
  write(1, "|", 1);
  write(1, "0123456789", 10);
  write(1, "|", 1);
  write(1, "XY", 1);
  write(1, "\n", 1);
  return 0;
}
