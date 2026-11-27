// error: defer across await cannot resize external K storage or replace external references
var values: i64[>..<] = [1, 2, 3];

async fn shrink() {
    defer { values.pop(); }
    await yield_now();
}

fn main() {
    let view = task_scope(shrink()) { scope =>
        scope.step();
        values[0..3]
    };
    print(view[2]);
}
