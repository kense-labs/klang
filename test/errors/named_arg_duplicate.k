// error: duplicate argument name: a
fn f(a: i64, b: i64) -> i64 { return a + b; }
fn main() {
    let _x = f(a: 1, a: 2);
}
