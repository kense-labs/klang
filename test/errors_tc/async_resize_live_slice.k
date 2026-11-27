// error: cannot resize through a task borrow of values while view
async fn discard(values: i64[>..<]&) {
    await yield_now();
    values.clear();
}

async fn caller() -> i64 {
    var values: i64[>..<] = [3, 9];
    let view = values[..];
    await discard(values);
    return view[0];
}

fn main() { print(caller().join()); }
