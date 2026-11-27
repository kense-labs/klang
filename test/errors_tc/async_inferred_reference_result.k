// error: an async result needs a fixed-size type without references
async fn message() { "temporary view" }

fn main() {
    let task = message();
    print(task.join());
}
