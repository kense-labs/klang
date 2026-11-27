import std;

root struct Holder { borrowed: i64[>..<], neighbor: i64[>..<] }
async fn item(value: const i64&) -> i64 { await yield_now(); return value; }
fn shrink(h: Holder&) { h.borrowed.clear(); }

fn main() {
    var state = Holder { [5], [10] };
    task_scope(item(state.borrowed[0])) { scope =>
        state.neighbor.clear();
        print("neighbor ", state.neighbor.len);
        assert(scope.step());
        shrink(state);
        print(scope.join(0));
    }
}
