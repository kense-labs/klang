// error: no matching overload for call to f
fn f(a: i64, b: i64 = 2) -> i64 { return a + b; }
fn main() {
    let _x = f(a: 1, unknown: 3);
}
