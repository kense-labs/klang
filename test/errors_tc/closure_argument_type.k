// error: argument 1: cannot pass const u8[:] as i64
fn main() {
    let twice = fn(x: i64) -> i64 { x * 2 };
    print(twice("bad"));
}
