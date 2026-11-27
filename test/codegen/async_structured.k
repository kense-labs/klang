import std;

async fn leaf(x: i64) -> i64 {
    defer { print("child cleanup ", x); }
    await yield_now();
    return x * 2;
}

fn choose(value: bool) -> bool {
    print("condition ", value);
    return value;
}

async fn walk(take: bool, count: i64) -> i64 {
    var sum = 0;
    defer { print("parent ", sum); }
    if choose(take) {
        defer { print("branch ", sum); }
        let value = await leaf(20);
        sum += value;
    } else {
        sum++;
    }
    var index = 0;
    while index < count {
        let before = index;
        defer { print("iteration ", before, " ", sum); }
        index++;
        if index % 2 == 0 {
            await yield_now();
            continue;
        }
        let value = await leaf(index);
        sum += value;
        if index == 3 { break; }
    }
    return sum;
}

fn bound(value: i64) -> i64 {
    print("bound ", value);
    return value;
}

async fn counted() -> i64 {
    var sum = 0;
    for n, index in bound(2)..bound(5) {
        defer { print("counted ", n, " ", index); }
        let value = await yield_now(n + index);
        sum += value;
    }
    for small: u8 in 254..256 {
        await yield_now();
        sum += small;
    }
    return sum;
}

async fn outer(early: bool) -> i64 {
    loop {
        defer { print("loop cleanup"); }
        await yield_now();
        if early { return 7; }
        break;
    }
    return 8;
}

fn main() {
    print("joined ", walk(true, 4).join());
    print("skipped ", walk(false, 2).join());
    task_scope(walk(true, 4)) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    task_scope(walk(false, 2)) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    print("range ", counted().join());
    print("early ", outer(true).join());
    print("break ", outer(false).join());
}
