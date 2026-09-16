// Iterative factorial.
fn fact(n: int) -> int {
  let acc: int = 1;
  while (n > 1) {
    acc = acc * n;
    n = n - 1;
  }
  return acc;
}

fn main() -> int {
  print(fact(5));   // 120
  print(fact(10));  // 3628800
  return 0;
}
