// error: a borrowed callable cannot depend on external mutable bindings or change borrowed storage layout
var total: i64 = 1;
fn add(value: i64) -> i64 { total += value; return total; }
fn main() {
    let f = borrow_callable<fn&(i64) -> i64>(add);
    print(f(5));
}
