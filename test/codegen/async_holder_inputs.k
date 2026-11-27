struct Window { rows: const i64[:], mark: i64& }
struct Batch { windows: Window[2], extra: const i64? }

async fn sum(input: Batch) -> i64 {
    let first = input.windows[0];
    defer { print("cleanup ", first.rows[0], " ", input.windows[1].rows[0]); }
    await yield_now();
    first.mark += 1;
    let extra .= input.extra;
    return first.rows[0] + input.windows[1].rows[0] + (if extra { extra } else { 0 });
}

async fn forward(input: Batch) -> i64 {
    await yield_now();
    return await sum(input);
}

async fn local(seed: i64) -> i64 {
    var left: i64[>..] = [seed];
    var right: i64[..4] = [8, 9];
    var lmark: i64 = 4;
    var rmark: i64 = 5;
    let input = Batch { [Window { left[..], lmark }, Window { right[..], rmark }], null };
    await yield_now();
    let result = await forward(input);
    return result + lmark;
}

fn main() {
    var left: i64[>..] = [11];
    var right: i64[..4] = [21, 22];
    var repl: i64[2] = [100, 101];
    var a: i64 = 1;
    var b: i64 = 2;
    let extra: i64 = 3;
    var input = Batch { [Window { left[..], a }, Window { right[..], b }], extra };
    print("joined ", forward(input).join(), " mark ", a);

    task_scope(sum(input)) { scope =>
        assert(scope.step());
        input.windows[0].rows = repl[..];
        right[0] = 25;
        print("scoped ", scope.join(0));
    }
    assert(a == 3 && input.windows[0].rows[0] == 100);

    task_scope(sum(input)) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    print("local ", local(31).join());
}
