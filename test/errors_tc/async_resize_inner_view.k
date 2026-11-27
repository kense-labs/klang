// error: cannot call wipe, which shrinks values while view
fn wipe(values: i64[>..<]&) { values.clear(); }

async fn stale(values: i64[>..<]&) -> i64 {
    let view = values[..];
    await yield_now();
    wipe(values);
    return view[0];
}

fn main() {
    var values: i64[>..<] = [6];
    print(stale(values).join());
}
