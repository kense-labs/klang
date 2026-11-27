// error: cannot .clear through a non-writable value
async fn clear(values: const i64[>..<]&) {
    await yield_now();
    values.clear();
}

fn main() {
    var values: i64[>..<] = [41];
    clear(values).join();
}
