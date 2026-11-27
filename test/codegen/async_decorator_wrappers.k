import std;

decorator Outer(label: const u8[:], pause: bool) on async fn {
    wrap {
        print("enter ", label);
        defer print("leave ", label);
        if pause { await yield_now(); }
        return (await next()) + 3;
    }
}

fn argument() -> i64 { print("argument"); return 6; }

decorator Inner() on async fn {
    wrap {
        print("enter inner");
        defer print("leave inner");
        return await next();
    }
}

@Outer("outer", true)
@Inner
async fn work(value: i64 = argument()) {
    print("body ", value);
    defer print("leaf cleanup");
    await yield_now();
    return value * 2;
}

@Inner
async fn change(values: i64[>..<]&, target: i64&, fallback: const i64?) {
    await yield_now();
    values.push(12);
    target += values[0];
    if fallback { target += fallback; }
    return target;
}

async fn parent() {
    var values: i64[>..<] = [5];
    var target = 10;
    let result = await change(values, target, null);
    assert(values.len == 2 && values[1] == 12);
    assert(result == target);
    return target;
}

struct View { items: const i64[:] }

decorator Branch(take_first: bool) on async fn(i64) -> i64 {
    wrap {
        if take_first { return await next(); }
        await yield_now();
        return await next();
    }
}

@Branch(false)
async fn increment(value: i64) -> i64 { return value + 1; }

@Inner
async fn first(view: const View&) -> i64 {
    await yield_now();
    return view.items[0];
}

fn main() {
    let pending = work();
    print("prepared ", decorator_metadata<Outer>(work).label);
    print("join ", pending.join());
    task_scope(work(7)) { scope =>
        scope.cancel(0);
    }
    task_scope(work(8)) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    task_scope(work(9)) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    print("parent ", parent().join());
    var values: i64[>..<] = [4];
    var target = 17;
    let fallback = 2;
    task_scope(change(values, target, fallback)) { scope =>
        print("scoped ", scope.join(0));
    }
    assert(values.len == 2 && target == 23);
    let stable: i64[1] = [4];
    let view = View { stable[..] };
    print("holder ", first(view).join());
    print("branch ", increment(8).join());
}
