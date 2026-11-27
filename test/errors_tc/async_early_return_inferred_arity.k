// error: returning 1 value(s)
async fn inconsistent(stop: bool) {
    if stop { return; }
    await yield_now();
    return 10;
}

fn main() { inconsistent(false).join(); }
