import std;

root struct Holder { borrowed: i64[>..<], neighbor: i64[>..<] }
var state = Holder { [5, 10], [10] };

async fn watch() -> i64 {
    var sum = 0;
    for value in state.borrowed[1..] {
        await yield_now();
        sum += value;
    }
    return sum;
}

fn shrink(h: Holder&) { h.borrowed.pop_n(1); }
async fn change() { shrink(state); }

fn main() {
    task_scope(watch(), change()) { scope =>
        assert(scope.step());
        scope.join(1);
    }
}
