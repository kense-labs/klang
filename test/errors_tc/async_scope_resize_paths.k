// error: values is exclusively borrowed by a scoped task
async fn reset(values: i64[..4]&) { values.clear(); }

fn run(complete: bool) {
    var values: i64[..4] = [6, 9];
    task_scope(reset(values)) { scope =>
        if complete { scope.join(0); }
        values.push(2);
    }
}

fn main() { run(true); }
