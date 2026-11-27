// error: borrow_callable needs a closure in stable storage at every call site
fn make(value: i64) { fn[value: i64]() -> i64 { value } }
fn borrow<T>(environment: const T&) -> fn&() -> i64 {
    return borrow_callable<fn&() -> i64>(environment);
}

fn main() {
    var environments = [];
    environments.push(make(3));
    let callback = borrow(environments[0]);
    environments.clear();
    print(callback());
}
