// error: unknown identifier: missing_capture
fn main() {
    let missing_capture = 9;
    let closure = fn() -> i64 { missing_capture };
    print(closure());
}
