// error: decorator argument requires a scalar or string constant
decorator Route(version: u32) on fn;
fn choose() -> u32 { 7 }
@Route(choose())
fn handler() {}
fn main() {}
