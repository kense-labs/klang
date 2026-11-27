import std;

async fn change(values: i64[>..<]&) -> i64 {
    values.push(11);
    await yield_now();
    let top = values.pop();
    values.resize(1);
    values.push(top + 1);
    return values[0] + values[1];
}

async fn relay(values: i64[>..<]&) -> i64 {
    return await change(values);
}

root struct Work { values: i64[>..<], labels: i64[>..<] }

async fn local() -> i64 {
    var work = Work { [11, 33, 56], [5] };
    let labels = work.labels[..];
    let total = await relay(work.values);
    assert(work.values.len == 2 && work.values[1] == 12);
    return total + labels[0];
}

fn clear(values: i64[>..]&) { values.clear(); }

async fn grow(values: i64[>..]&) -> i64 {
    await yield_now();
    clear(values);
    values.push(31);
    return values[0];
}

async fn limited(values: i64[..4]&) -> i64 {
    await yield_now();
    values.clear();
    values.push(33);
    return values[0];
}

async fn runtime_limit(values: i64[..]&) -> i64 {
    await yield_now();
    values.resize(1);
    values.push(7);
    return values[0] + values[1] + values.cap;
}

async fn optional(values: i64[>..<]?) -> i64 {
    await yield_now();
    if values {
        values.clear();
        values.push(22);
        return values.len;
    }
    return 0;
}

async fn hold(values: i64[>..<]&) {
    defer {
        values.resize(1);
        print("child cleanup ", values.len, " ", values[0]);
    }
    await yield_now();
    values.push(99);
}

async fn cancelled() {
    var values: i64[>..<] = [61, 62];
    defer { print("parent cleanup ", values.len, " ", values[0]); }
    await hold(values);
}

fn main() {
    var changing: i64[>..<] = [2, 3, 4];
    print("direct ", change(changing).join(), " len ", changing.len);
    print("local ", local().join());

    var growing: i64[>..] = [12, 55];
    print("grow ", grow(growing).join(), " len ", growing.len);

    var fixed: i64[..4] = [3, 4];
    var reserved: i64[..] = [..4];
    reserved.push(6);
    reserved.push(99);
    print("limited ", limited(fixed).join(), " ", runtime_limit(reserved).join());
    assert(fixed.len == 1 && reserved.len == 2);

    print("optional ", optional(changing).join(), " ", optional(null).join());
    assert(changing[0] == 22);

    task_scope(cancelled()) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
}
