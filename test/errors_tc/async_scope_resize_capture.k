// error: values is exclusively borrowed by a scoped task
async fn reset(values: i64[..4]&) { values.clear(); }

fn main() {
    var values: i64[..4] = [6, 9];
    let view = values[..];
    fn first() -> i64 { return view[0]; }
    task_scope(reset(values)) { scope =>
        print(first());
        scope.join(0);
    }
}
