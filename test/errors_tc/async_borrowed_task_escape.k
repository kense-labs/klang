// error: a task with borrowed arguments needs direct await, join, or task_scope
async fn read(value: const i64&) -> i64 {
    await yield_now();
    return value;
}

fn main() {
    let value = 41;
    let task = read(value);
    print(task.join());
}
