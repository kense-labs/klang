async fn read(values: const i64[>..<]&) -> i64 {
    let view = values[..];
    await yield_now();
    return view[0];
}

async fn discard(values: i64[>..<]&) {
    await yield_now();
    values.clear();
}

fn main() {
    var values: i64[>..<] = [41];
    task_scope(read(values)) { scope =>
        assert(scope.step());
        print("pending");
        discard(values).join();
    }
}
