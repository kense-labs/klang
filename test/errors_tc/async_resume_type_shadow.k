// error: array size length names a local variable, not a constant
let length = 1;

async fn check_size() -> i64 {
    let length: i64 = 2;
    await yield_now();
    let values: i64[length] = [3];
    return values[0];
}

fn main() { print(check_size().join()); }
