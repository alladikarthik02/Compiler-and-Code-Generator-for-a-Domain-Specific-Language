// Everything here is statically computable -> should fold to two prints.
fn main() -> int {
  let a: int = 2 + 3 * 4;      // 14
  let b: int = (10 - 4) * 5;   // 30
  let c: int = a * 0 + b - 0;  // 30   (algebraic: *0 -> 0, -0 -> id)
  let unused: int = 99 * 7;    // dead -> eliminated
  print(a);
  print(c);
  return 0;
}
