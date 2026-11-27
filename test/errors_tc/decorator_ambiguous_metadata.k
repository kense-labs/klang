// error: decorator Route matches multiple overloads
decorator Route(version: u8) on fn;
@Route(1)
fn handler(a: i64) -> i64 { a }
@Route(2)
fn handler(a: f64) -> f64 { a }
fn main() { print(decorator_metadata<Route>(handler).version); }
