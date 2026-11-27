struct Window { rows: const i64[:], mark: i64&, extra: const i64?, bias: i64 }

async fn read(input: const Window&, offset: i64) -> i64 {
    defer { print("cleanup ", input.rows[0], " ", input.mark); }
    await yield_now();
    input.mark += 1;
    let extra .= input.extra;
    return input.rows[0] + input.bias + offset + (if extra { extra } else { 0 });
}

async fn forward(input: const Window&, offset: i64) -> i64 {
    return await read(input, offset);
}

async fn local() -> i64 {
    var data: i64[>..] = [40];
    var mark: i64 = 9;
    let input = Window { data[..], mark, null, 2 };
    let borrowed: const Window& = input;
    await yield_now();
    return await forward(borrowed, 3);
}

fn main() {
    var first: i64[..4] = [11, 12];
    var second: i64[..4] = [21, 22];
    var mark: i64 = 1;
    let extra: i64 = 3;
    var input = Window { first[..], mark, extra, 4 };
    print("direct ", forward(input, 2).join());

    task_scope(forward(input, { input.rows = second[..]; 1 })) { scope =>
        assert(scope.step());
        second[0] = 25;
        input.bias = 7;
        input.rows = { print("scoped ", scope.join(0)); first[..] };
    }
    assert(mark == 3 && input.rows[0] == 11);

    task_scope(forward(input, 5)) { scope =>
        assert(scope.step());
        scope.cancel(0);
        input.extra .= null;
    }
    print("local ", local().join());
}
