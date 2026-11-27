import std;

struct Wide { values: i64[32] }

async fn narrow(value: i16) -> i16 {
    await yield_now();
    return value + 1;
}

async fn wide(value: i64) -> Wide {
    await yield_now();
    return Wide { [value * 2; 32] };
}

async fn pipeline(input: i16) -> i64 {
    var current: i64 = input;
    let history: i64[32] = [current; 32];
    defer { print("outer ", current); }
    let first: i64 = await narrow(input);
    current += first;
    defer { print("inner ", current); }
    let next: i64 = await yield_now(current + 1);
    current += next;
    let final = await wide(current);
    return final.values[4] + next + history[3];
}

fn main() {
    print("complete ", pipeline(3).join());
    task_scope(pipeline(3)) { scope => scope.cancel(0); }
    print("cancelled before polling");
    task_scope(pipeline(3)) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    print("cancelled first");
    task_scope(pipeline(3)) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    print("cancelled second");
    task_scope(pipeline(3)) { scope =>
        assert(scope.step());
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    print("cancelled third");
}
