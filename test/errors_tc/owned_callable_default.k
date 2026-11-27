// error: a runtime callable requires an invocation target
fn main() {
    let callable = default<fn[0](i64) -> i64>();
    print(callable(1));
}
