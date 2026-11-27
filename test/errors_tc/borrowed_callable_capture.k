// error: borrowed callable captures need read-only references to stable fixed storage
fn main() {
    var ints: i64[1] = [5];
    let first = fn[x: i64& = ints[0]]() -> i64 { x };
    let f = borrow_callable<fn&() -> i64>(first);
    print(f());
}
