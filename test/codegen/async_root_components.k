import std;

root struct State { first: i64[>..<], second: i64[>..<], fixed: i64[..4], count: i64 }
var state = State { [10, 20], [31], [50, 60], 7 };

async fn observe() -> i64 {
    let view = state.first[..];
    defer { print("global cleanup ", view[1]); }
    await yield_now();
    return view[0] + view.len;
}

async fn sibling() {
    state.second.clear();
    state.second.push(42);
    state.count += 1;
}

async fn scan() -> i64 {
    var total = 0;
    for value in state.fixed[1..] {
        await yield_now();
        total += value;
    }
    return total;
}

async fn read(value: const i64&) -> i64 { await yield_now(); return value; }

async fn local() -> i64 {
    var cache = State { [41], [100], [2], 0 };
    let item .= cache.first[0];
    defer { print("local cleanup ", item); }
    await yield_now();
    item += 1;
    return item;
}

fn tally(s: State&) -> i64 { s.first[0] + s.second[0] + s.fixed[0] + s.count }

async fn whole() -> i64 {
    var s = State { [12], [13], [14], 2 };
    let alias .= s;
    await yield_now();
    let initial = tally(alias);
    s.count = await yield_now(3);
    return initial + tally(alias);
}

root struct Views { view: i64[:], item: i64&, missing: i64? }
fn total(s: Views&) -> i64 { s.view[1] + s.item }

async fn stored() -> i64 {
    var source: i64[>..] = [20, 22];
    var view = Views { view: source[..], item: source[0] };
    let alias .= view;
    await yield_now();
    let result = total(alias);
    await yield_now();
    assert(view.missing == null);
    return result + view.view[0];
}

fn main() {
    task_scope(observe(), sibling(), scan()) { scope =>
        assert(scope.step());
        scope.join(1);
        assert(state.second[0] == 42 && state.count == 8);
        print("observe ", scope.join(0));
        print("range ", scope.join(2));
    }
    task_scope(read(state.first[0])) { scope =>
        state.second.clear();
        print("scoped ", scope.join(0));
        state.first.clear();
    }
    print("local ", local().join());
    task_scope(local()) { scope => assert(scope.step()); scope.cancel(0); }
    print("whole ", whole().join());
    print("stored ", stored().join());
}
