fn gcd(a: int, b: int) -> int {
  while (b != 0) {
    let t: int = b;
    b = a % b;
    a = t;
  }
  return a;
}

fn main() -> int {
  print(gcd(48, 36));
  return 0;
}
