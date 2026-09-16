// Find and count all prime numbers up to 30.

// Returns true if n is prime, false otherwise.
fn is_prime(n: int) -> bool {
  if (n < 2) { return false; }        // 0 and 1 are not prime
  let d: int = 2;
  while (d * d <= n) {                // only test divisors up to sqrt(n)
    if (n % d == 0) { return false; } // divides evenly -> not prime
    d = d + 1;
  }
  return true;
}

fn main() -> int {
  let n: int = 2;
  let count: int = 0;
  while (n <= 30) {
    if (is_prime(n)) {                // condition is a bool -> legal
      print(n);
      count = count + 1;
    }
    n = n + 1;
  }
  print(count);                       // how many primes we found
  return 0;
}
