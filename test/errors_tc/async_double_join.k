// error: owned resource task has already moved
async fn make() -> i64 { 1 }

fn main() {
    let task = make();
    print(task.join());
    print(task.join());
}
