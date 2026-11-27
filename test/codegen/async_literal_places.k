import std;

enum Event { Empty, Number { value: i64 }, Pair { x: i64, y: i64 } }
struct Packet { first: i64, values: i64[3], event: Event }
struct Cell { value: i64 }

var packet = Packet { 1, [2, 3, 4], Event.Empty };
var matrix: Cell[2][2] = [[Cell { 10 }, Cell { 20 }], [Cell { 30 }, Cell { 40 }]];
var row = 0;

fn record(tag: i64, value: i64) -> i64 {
    print("read ", tag, " ", value);
    return value;
}

async fn value(tag: i64, n: i64) -> i64 {
    print("start ", tag);
    defer { print("finish ", tag); }
    await yield_now();
    return n;
}

async fn publish() {
    defer { print("packet ", packet.first, " ", packet.values[1], " ", packet.event); }
    packet = Packet {
        first: record(1, 5),
        values: [record(2, 6), await value(3, 7), record(4, 8)],
        event: Event.Number { value: await value(5, 9) },
    };
}

async fn column() -> i64 {
    await yield_now();
    row = 1;
    return 1;
}

async fn delta() -> i64 {
    await yield_now();
    matrix[0][1].value = 100;
    return 6;
}

async fn update() -> i64 {
    matrix[record(6, row)][await column()].value += await delta();
    return matrix[await yield_now(0)][await yield_now(1)].value;
}

async fn local() -> i64 {
    var values: i64[3] = [10, 20, 30];
    defer { print("local ", values); }
    values[await yield_now(1)] = await yield_now(25);
    values[await yield_now(0)]++;
    return values[2];
}

async fn large_fill() -> i64 {
    let scratch: i64[600] = [await yield_now(record(7, 12)); 600];
    return await yield_now(scratch[599]);
}

async fn variable_arrays() -> i64 {
    var values: i64[>..] = [record(8, 13), await yield_now(14)];
    values.push(15);
    let sum = values[0] + values[1] + values[2];
    let more: i64[] = [await yield_now(21), 23];
    return sum + more[0] + more[1];
}

fn main() {
    task_scope(publish()) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    publish().join();
    print("update ", update().join(), " ", matrix[1][1].value);
    task_scope(local()) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    print("local result ", local().join());
    print("fill ", large_fill().join());
    print("variable ", variable_arrays().join());
}
