import std;

struct Batch { values: i64[4] }
enum Mode { Data { value: i64 }, Empty }

async fn doubled(value: i64) -> i64 {
    defer { print("child ", value); }
    await yield_now();
    return value * 2;
}

async fn direct(value: i64) -> i64 {
    return await doubled(value);
}

async fn select(mode: Mode) -> i64 {
    let result = match mode {
        Data item => {
            defer { print("data ", item.value); }
            await yield_now();
            item.value
        },
        Empty => 5,
    };
    return block { break await doubled(result); };
}

async fn array_steps(limit: i64) -> i64 {
    var batch = Batch { [4, 7, 10, 13] };
    var total = 0;
    for value, index in batch.values {
        defer { print("element ", index); }
        let carried = await yield_now(value * 2 + index);
        total += carried;
        if index == limit { return total; }
        if index == 0 { batch.values[1] = 8; }
    }
    return total;
}

fn main() {
    print("direct ", direct(2).join());
    print("selected ", select(Mode.Data { value: 3 }).join());
    print("empty ", select(Mode.Empty).join());
    task_scope(select(Mode.Data { value: 4 })) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    print("all ", array_steps(-1).join());
    print("early ", array_steps(1).join());
    task_scope(array_steps(-1)) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
}
