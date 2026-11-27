// error: cannot clear values: values[0], evaluated earlier in the statement, may still refer into values
async fn read(value: const i64&, extra: i64) -> i64 {
    await yield_now();
    return value + extra;
}

async fn parent() -> i64 {
    var values: i64[>..<] = [21, 22];
    return await read(values[0], { values.clear(); await yield_now(20) });
}

fn main() { print(parent().join()); }
