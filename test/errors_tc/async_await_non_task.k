// error: await needs an owned task constructed by an async fn
async fn wait_for_number() -> i64 {
    let number = 42;
    let result = await number;
    return result;
}

fn main() { print(wait_for_number().join()); }
