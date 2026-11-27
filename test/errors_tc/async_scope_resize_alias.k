// error: values is exclusively borrowed by a scoped task
async fn reset(values: i64[..4]&) { values.clear(); }

fn main() {
    var values: i64[..4] = [6, 9];
    let view = values[..];
    task_scope(reset(values)) { scope =>
        print(view[0]);
        scope.join(0);
    }
}
