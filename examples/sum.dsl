// Sum 1..100 the slow way, so the optimizer has real control flow to preserve.
fn main() -> int {
  let i: int = 1;
  let total: int = 0;
  while (i <= 100) {
    total = total + i;
    i = i + 1;
  }
  print(total);  // 5050
  return 0;
}
