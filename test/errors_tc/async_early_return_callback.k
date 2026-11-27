// error: an async poll cannot return through a separate function call
fn invoke<Body>(value: i64) -> i64 { Body(value) }

async fn route(stop: bool) -> i64 {
    let result: i64 = invoke(9) { value =>
        if stop { return value; }
        value + 1
    };
    await yield_now();
    return result;
}

fn main() { print(route(true).join()); }
