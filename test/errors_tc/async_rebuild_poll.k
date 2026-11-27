// error: a task may use bucket, and that value is built over the old contents of bucket
import std;

struct Bucket { marker: i64, list: i64[>..<] }
var bucket: Bucket = Bucket { 1, [41, 42] };

async fn waiting() {
    let view = bucket.list[..];
    await yield_now();
    print(view[0]);
}

fn main() {
    task_scope(waiting()) { scope =>
        bucket = {
            scope.step();
            Bucket { 2, [90] }
        };
        scope.join(0);
    }
}
