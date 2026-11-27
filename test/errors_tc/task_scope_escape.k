// error: a task scope controller cannot be used as a value
async fn value() -> i64 { 17 }

fn main() {
    let escape = task_scope(value()) { scope => scope };
    print(escape);
}
