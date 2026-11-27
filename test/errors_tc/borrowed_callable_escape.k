// error: returning a reference rooted in add, which dies with this function
fn escape() -> fn&(i64) -> i64 {
    let add = fn[bias: i64 = 4](value: i64) -> i64 { bias + value };
    return borrow_callable<fn&(i64) -> i64>(add);
}
fn main() { print(escape()(1)); }
