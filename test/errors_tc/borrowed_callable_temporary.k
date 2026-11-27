// error: borrow_callable needs a closure in stable storage
fn main() {
    let f = borrow_callable<fn&(i64) -> i64>(fn(value: i64) -> i64 { value + 1 });
    print(f(0));
}
