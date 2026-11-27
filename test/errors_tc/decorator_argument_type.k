// error: does not fit u8
decorator Route(version: u8) on fn;
@Route(256)
fn handler() {}
fn main() {}
