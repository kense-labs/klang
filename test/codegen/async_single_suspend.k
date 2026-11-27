import std;

struct Pair { first: i64, second: i64 }

fn note(x: i64) -> i64 { print("argument ", x); x }
fn forward<T>(task: T) -> T { task }

async fn compute(var x: i64, extra: i64 = 6) -> Pair {
    print("start ", x);
    var total: i64 = x + extra;
    x += 2;
    total *= 3;
    let received: i64 = await yield_now(note(5));
    print("resume ", x);
    return Pair { total, received };
}

async fn tick() {
    await yield_now();
    print("tick");
}

async fn immediate(x: i64) -> i64 { x / 2 }

fn main() {
    let task = compute(note(4));
    print("created");
    let result = forward(task).join();
    print(result.first, " ", result.second);
    tick().join();
    print(immediate(18).join());
}
