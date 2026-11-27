import std;

var shared: i64[>..<] = [41, 42];

async fn hold() -> i64 {
    let item .= shared[1];
    await yield_now();
    return item;
}

fn shrink(values: i64[>..<]&) { values.pop_n(1); }
async fn remove() { shrink(shared); }

fn main() {
    task_scope(hold(), remove()) { scope =>
        assert(scope.step());
        scope.step();
    }
}
