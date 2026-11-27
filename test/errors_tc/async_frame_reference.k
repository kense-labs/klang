// error: forward a writable task input with direct await or join
async fn async_view(view: i64[>..]&) -> i64 {
    await yield_now();
    return view[0];
}

async fn forward(view: i64[>..]&) -> i64 {
    return task_scope(async_view(view)) { scope => scope.join(0) };
}

fn main() {
    var values: i64[>..] = [41];
    print(forward(values).join());
}
