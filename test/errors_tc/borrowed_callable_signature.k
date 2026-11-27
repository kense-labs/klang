// error: target parameters do not match the borrowed callable signature
fn main() {
    let negate = fn(value: i64) -> i64 { -value };
    let wrong = borrow_callable<fn&(f64) -> i64>(negate);
    print(wrong(2.0));
}
