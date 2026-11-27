import std;

reusable var slots: i64[>..] = [10, 20];
reusable[] var runs: i64[>..] = [];

async fn read(value: const i64&) -> i64 {
    await yield_now();
    return value;
}

async fn watch_slot() -> i64 {
    let pool .= slots;
    let value .= pool[0];
    await yield_now();
    assert(pool.index_of(value) == 0);
    return value;
}

async fn sample() -> i64 {
    var sum = 0;
    for value in slots[1..] {
        await yield_now();
        sum += value;
    }
    return sum;
}

async fn watch_run(sample: bool) -> i64 {
    if sample {
        let view = runs[..];
        await yield_now();
        return view[0] + view[1] + view.len;
    }
    return 0;
}

fn with_offset(values: const i64[:], offset: i64) -> i64 { values[0] + offset }

async fn staged() -> i64 { return with_offset(slots[..], await yield_now(5)); }

async fn write_after_walk() -> i64 {
    let view = slots[..];
    var sum = 0;
    for value in view {
        sum += value;
        await yield_now();
    }
    view[0] = await yield_now(45);
    return sum;
}

async fn read_global() -> i64 { return await read(slots[0]); }

async fn local_slot() -> i64 {
    reusable var pool: i64[>..] = [11, 22];
    let alias .= pool;
    let value .= alias[1];
    alias.free(0);
    defer {
        let index = alias.alloc_index(90);
        print("slot cleanup ", index, " ", value);
    }
    await yield_now();
    assert(alias.alloc_index(31) == 0);
    return await read(value);
}

async fn local_run() -> i64 {
    reusable[] var pool: i64[>..] = [];
    let alias .= pool;
    let view = alias.alloc_slice(2);
    view[1] = 19;
    let spare = alias.alloc_slice(3);
    spare[0] = 75;
    alias.free_slice(spare);
    await yield_now();
    let reused = alias.alloc_slice(3);
    assert(alias.index_of(reused[0]) == 2 && reused[0] == 0);
    return await read(view[1]);
}

fn main() {
    let first = runs.alloc_slice(2);
    let second = runs.alloc_slice(2);
    first[0] = 8;
    first[1] = 21;
    second[0] = 29;
    task_scope(watch_slot(), sample(), watch_run(true), read(slots[0])) { scope =>
        assert(scope.step());
        assert(scope.step());
        assert(scope.step());
        assert(scope.step());
        slots.free(0);
        assert(slots.alloc_index(42) == 0);
        slots.push(80);
        let moved = runs.realloc_slice(first, 3);
        assert(runs.index_of(moved[0]) == 4 && moved[1] == 21 && moved[2] == 0);
        assert(second[0] == 29);
        print("slot ", scope.join(0), " ", scope.join(3));
        print("sample ", scope.join(1));
        print("run ", scope.join(2));
    }
    assert(write_after_walk().join() == 142);
    assert(read_global().join() == 45);
    assert(staged().join() == 50);
    print("local slot ", local_slot().join());
    task_scope(local_slot()) { scope => assert(scope.step()); scope.cancel(0); }
    task_scope(local_slot()) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    print("local run ", local_run().join());
}
