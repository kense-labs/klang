// error: a decorator continuation is called as next() and forwards the current arguments
decorator Explicit() on fn { wrap { return next(8); } }
@Explicit
fn run(value: i64) -> i64 { return value; }
fn main() { print(run(1)); }
