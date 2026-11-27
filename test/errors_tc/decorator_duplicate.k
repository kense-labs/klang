// error: decorator Route is applied more than once
decorator Route() on fn;
@Route
@Route
fn handler() {}
fn main() {}
