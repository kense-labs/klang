// error: decorator Route needs a checkable function body for signature fn()
decorator Route() on fn();
@Route
fn handler() {
    if false { return 7 from register; }
    42
}
fn register() -> i64 {
    let data = decorator_metadata<Route>(handler);
    return 0;
}
fn main() { print(register()); }
