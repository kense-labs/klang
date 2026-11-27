import std;

async fn inspect(values: const i64[:], output: i64&, fallback: const i64?) -> i64 {
    defer { print("child cleanup ", values[0]); }
    await yield_now();
    output += values[0];
    if fallback { output += fallback; }
    return output;
}

async fn forward(values: const i64[:], output: i64&) -> i64 {
    return await inspect(values, output, null);
}

async fn parent() -> i64 {
    var output = 40;
    let values: i64[>..] = [2, 20];
    defer { print("parent cleanup ", output); }
    return await forward(values[..], output);
}

async fn increment(value: i64&, delta: i64) -> i64 {
    await yield_now();
    value += delta;
    return value;
}

async fn local_buffer() -> i64 {
    var data: i64[>..<] = [41];
    let saved = await increment(data[0], 1);
    data.clear();
    return saved;
}

async fn delayed(value: i64) -> i64 {
    await yield_now();
    return value;
}

async fn sum(input: const i64[:], starting: i64) -> i64 {
    var total = starting;
    for value in input {
        total += value;
        await yield_now();
    }
    return total;
}

async fn staged() -> i64 {
    let values: i64[>..] = [10, 20];
    return await sum(values[..], await delayed(12));
}

fn main() {
    var output = 39;
    let values: i64[1] = [2];
    let fallback = 1;
    print("join ", inspect(values, output, fallback).join());
    print("await ", parent().join());
    task_scope(parent()) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    print("buffer ", local_buffer().join());
    print("staged ", staged().join());
    task_scope(increment(output, 1), increment(output, 2)) { scope =>
        scope.join(0);
        scope.join(1);
    }
    print("scoped ", output);
}
