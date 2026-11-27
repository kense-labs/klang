// error: cannot resize values while view
async fn invalid() {
    var values: i64[>..<] = [1, 2, 3];
    let view = values[1..];
    await yield_now();
    values.resize(1);
    print(view[0]);
}

fn main() { invalid().join(); }
