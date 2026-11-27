// error: this scoped task may already have been consumed
async fn value() -> i64 { 17 }

fn main() {
    task_scope(value()) { scope =>
        scope.join(0);
        scope.join(0);
    }
}
