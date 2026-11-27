// error: closure captures are private
fn main() {
    let closure = fn[seed: i64 = 2]() -> i64 { seed };
    print(closure.seed);
}
