async fn watch(values: const i64[..]&) -> i64 {
    let view = values[..];
    await yield_now();
    return view[0];
}

fn shrink(values: i64[..]&) { values.clear(); }

fn main() {
    var values: i64[..] = [..4];
    values.push(41);
    task_scope(watch(values)) { scope =>
        assert(scope.step());
        print("pending ", values.len);
        shrink(values);
    }
}
