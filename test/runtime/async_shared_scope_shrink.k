import std;

async fn read(value: const i64&) -> i64 {
    await yield_now();
    return value;
}

fn main() {
    var values: i64[>..<] = [41];
    task_scope(read(values[0])) { scope =>
        values.clear();
        print(scope.join(0));
    }
}
