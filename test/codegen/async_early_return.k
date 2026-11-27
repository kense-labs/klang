import std;

struct Result { values: i64[32] }

async fn child(value: i16) -> i16 {
    print("child started");
    defer { print("child cleanup"); }
    await yield_now();
    return value + 1;
}

async fn route(mode: i32) -> Result {
    var progress: i64 = 10;
    defer { print("outer ", progress); }
    if mode == 1 {
        defer { print("branch ", progress); progress++; }
        return Result { [progress; 32] };
    }
    await yield_now();
    progress += 2;
    defer { print("after yield ", progress); }
    while mode == 2 {
        defer { print("iteration ", progress); }
        return Result { [progress; 32] };
    }
    let value: i64 = await child(6);
    progress += value;
    return Result { [progress; 32] };
}

async fn terminal() -> i64 {
    let answer = 41;
    defer { print("terminal cleanup"); }
    return answer;
    await child(1);
    return answer;
}

async fn dead_cooperative_site(stop: bool) -> i64 {
    let answer = 50;
    if stop { return answer; } else { return answer + 1; }
    await yield_now();
    return answer + 2;
}

async fn inferred_result(early: bool) {
    let wide: i64 = 54;
    if early { return wide; }
    await yield_now();
    let small: i16 = 55;
    return small;
}

async fn void_route(stop: bool) {
    defer { print("void cleanup"); }
    if stop { return; }
    await yield_now();
    print("void resumed");
}

async fn finish_after_child(early: bool) -> i64 {
    defer { print("parent cleanup"); }
    let value: i64 = await child(40);
    if early { return value; }
    await yield_now();
    return value + 1;
}

async fn leave_scope(early: bool) -> i64 {
    defer { print("scope owner cleanup"); }
    task_scope(child(4)) { scope =>
        assert(scope.step());
        if early { return 42; }
        scope.join(0);
    }
    await yield_now();
    return 43;
}

async fn use_scratch(early: bool) -> i64 {
    defer { print("scratch owner cleanup"); }
    {
        var bytes: u8[>..] = [];
        bytes.push(11);
        defer { bytes.push(12); print("scratch cleanup ", bytes.len); }
        if early { return bytes.len; }
    }
    await yield_now();
    return 2;
}

fn main() {
    print("branch result ", route(1).join().values[2]);
    print("loop result ", route(2).join().values[2]);
    print("normal result ", route(3).join().values[2]);
    print("terminal result ", terminal().join());
    print("dead suspension ", dead_cooperative_site(true).join(), " ", dead_cooperative_site(false).join());
    print("inferred result ", inferred_result(true).join(), " ", inferred_result(false).join());
    void_route(true).join();
    void_route(false).join();
    print("child result ", finish_after_child(true).join());
    print("scope result ", leave_scope(true).join());
    print("scratch result ", use_scratch(true).join());
    task_scope(route(3)) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    print("cancelled after yield");
    task_scope(route(3)) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    print("cancelled with child");
    {
        defer { print("nested join ", finish_after_child(true).join()); }
    }
}
