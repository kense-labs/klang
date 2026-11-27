// error: operator + cannot be applied to
fn inc(n: i64) -> i64 { n + 1 }
struct Box { call: fn[0](i64) -> i64 }
fn main() {
    let box = Box { own_callable<fn[0](i64) -> i64>(inc) };
    let invalid = box + box;
    print(invalid);
}
