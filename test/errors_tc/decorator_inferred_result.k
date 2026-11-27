// error: decorator Observe requires signature fn()
decorator Observe() on fn();
@Observe
fn handler() { 17 }
fn main() {}
