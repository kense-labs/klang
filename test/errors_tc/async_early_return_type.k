// error: expected a value of type i64
async fn invalid(stop: bool) -> i64 {
    if stop { return "wrong"; }
    await yield_now();
    return 0;
}

fn main() { print(invalid(true).join()); }
