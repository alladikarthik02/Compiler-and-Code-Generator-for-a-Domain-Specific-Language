// Collatz: repeatedly halve if even, else 3n+1. Count steps to reach 1.

fn is_even(n: int) -> bool {
  return n % 2 == 0;
}

fn collatz_steps(n: int) -> int {
  let steps: int = 0;
  while (n != 1) {
    if (is_even(n)) {
      n = n / 2;
    } else {
      n = 3 * n + 1;
    }
    steps = steps + 1;
  }
  return steps;
}

fn main() -> int {
  print(collatz_steps(6));    // 8 steps
  print(collatz_steps(27));   // 111 steps
  return 0;
}
