// error: ?. requires an optional receiver (T?), got S
struct S { x: i64 }
fn main() {
    let s = S { x: 1 };
    let _y = s?.x;
}
