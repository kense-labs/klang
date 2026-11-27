// error: decorator Route requires signature fn(i64) -> i64
decorator Route() on fn(i64) -> i64;
@Route
fn handler(input: u32) -> i64 { input }
fn main() {}
