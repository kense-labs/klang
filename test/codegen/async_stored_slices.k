import std;

let fixed_global: i64[3] = [5, 6, 7];

fn tail(data: i64[3]&) -> i64[:] { return data[1..]; }

async fn fixed() -> i64 {
    var data: i64[3] = [1, 2, 3];
    let view = tail(data);
    fn report() { print("cleanup fixed ", view[0]); }
    defer report();
    await yield_now();
    data[1] = 9;
    return view[0] + view[1];
}

async fn dynamic(seed: i64) -> i64 {
    var values: i64[>..<] = [seed, seed + 1, seed + 2];
    let view = values[1..];
    defer { print("cleanup dynamic ", view[0], " ", view[1]); }
    await yield_now();
    values[1] += 10;
    return view[0] + view[1];
}

async fn selected(short: bool) -> i64 {
    var left: i64[3] = [1, 2, 3];
    var right: i64[>..] = [10, 11, 12];
    var view = if short { left[..] } else { right[..] };
    var sum = 0;
    for value, index in view {
        await yield_now();
        sum += value;
        if index == 0 { view = left[1..]; }
    }
    return sum;
}

async fn large() -> i64 {
    let values: i64[600] = [7; 600];
    let last = values[598..];
    await yield_now();
    return last[1];
}

async fn global_view() -> i64 {
    let view = fixed_global[1..];
    await yield_now();
    return view[0] + view[1];
}

async fn indexed() -> i64 {
    var left: i64[3] = [1, 2, 3];
    var right: i64[>..] = [10, 20, 30];
    var view = left[..];
    let original = view[block {
        view = right[..];
        break await yield_now(1);
    }];
    view[await yield_now(0)] += block {
        view = left[..];
        break await yield_now(4);
    };
    return original * 100 + right[0] + view[0];
}

fn main() {
    print("fixed ", fixed().join());
    task_scope(fixed(), dynamic(20)) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
        scope.cancel(1);
    }
    print("dynamic ", dynamic(30).join());
    print("selected ", selected(true).join(), " ", selected(false).join());
    print("large ", large().join());
    print("global ", global_view().join());
    print("indexed ", indexed().join());
}
