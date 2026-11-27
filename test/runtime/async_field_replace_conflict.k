import std;

struct Bucket { list: i64[..4], marker: i64 }
var bucket: Bucket = Bucket { [41, 42], 1 };

async fn waiting() {
    let view = bucket.list[..];
    await yield_now();
    print(view[0]);
}

fn main() {
    task_scope(waiting()) { scope =>
        bucket = {
            scope.step();
            Bucket { [90], 2 }
        };
        scope.join(0);
    }
}
