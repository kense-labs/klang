// error: defer across await cannot resize external K storage or replace external references
var values: i64[>..<] = [1, 2, 3];

async fn invalid(flag: bool) {
    if flag {
        defer { values.pop(); }
        await yield_now();
    }
}

fn main() { invalid(true).join(); }
