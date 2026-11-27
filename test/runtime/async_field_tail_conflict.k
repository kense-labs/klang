import std;

struct Bucket { label: i64, list: i64[>..<] }
var bucket: Bucket = Bucket { 1, [41, 42] };

async fn waiting() {
    for value in bucket.list[1..] {
        await yield_now();
        print(value);
    }
}

fn truncate(list: i64[>..<]&) { list.pop_n(1); }
async fn change() { truncate(bucket.list); }

fn main() {
    task_scope(waiting(), change()) { scope =>
        assert(scope.step());
        scope.step();
    }
}
