import std;

var current = 10;

fn note(tag: i64, value: i64) -> i64 {
    print("arg ", tag);
    return value;
}

async fn signal(tag: i64, value: i64) -> i64 {
    print("start ", tag);
    defer { print("child cleanup ", tag); }
    await yield_now();
    current += 100;
    return value;
}

fn combine(a: i64, b: i64, c: i64) -> i64 {
    print("call");
    return a * 100 + b * 10 + c;
}

async fn expression() -> i64 {
    var stored = 3;
    defer { print("root ", stored); }
    stored = combine(note(1, current), await signal(2, 5) + await yield_now(note(3, 6)), note(4, current));
    return stored;
}

async fn predicate() -> bool {
    print("predicate");
    await yield_now();
    return true;
}

async fn controls(enable: bool) -> i64 {
    var total = 0;
    if enable && await predicate() { total++; }
    let skip = !enable || await predicate();
    assert(skip && await yield_now(true));
    var n = 0;
    while await yield_now(n < 3) { n += await yield_now(1); }
    for i in note(5, 1)..await yield_now(note(6, 4)) { total += i; }
    return match await yield_now(n) { 3 => total, _ => 0 };
}

struct Batch { values: i64[32] }
var batch = Batch { [20; 32] };

fn summary(value: Batch, next: i64) -> i64 { value.values[31] + next }

async fn change_batch() -> i64 {
    await yield_now();
    batch.values[31] = 99;
    return 3;
}

async fn aggregate() -> i64 {
    return summary(batch, await change_batch());
}

async fn render() {
    print(await yield_now(41) + 1);
}

fn main() {
    print("result ", expression().join());
    current = 10;
    task_scope(expression()) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    task_scope(expression()) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    print("enabled ", controls(true).join());
    print("disabled ", controls(false).join());
    print("aggregate ", aggregate().join(), " ", batch.values[31]);
    render().join();
}
