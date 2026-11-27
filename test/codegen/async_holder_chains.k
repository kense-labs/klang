struct Link { view: const i64[:], next: const Link? }
struct Start { first: const Link&, count: i64 }

var global_values: i64[..4] = [31, 32];
var global_first = Link { global_values[..], null };
var global_second = Link { global_values[1..], global_first };
var untouched: i64[..4] = [40];

async fn walk(first: const Link&, count: i64) -> i64 {
    defer { print("cleanup ", first.view[0]); }
    var cursor: const Link? = first;
    var total = 0;
    for i in count {
        if cursor {
            total += cursor.view[0];
            await yield_now();
            cursor .= cursor.next;
        }
    }
    return total;
}

async fn forwarded(input: Start) -> i64 {
    await yield_now();
    return await walk(input.first, input.count);
}

async fn local() -> i64 {
    var a: i64[>..] = [11];
    var b: i64[..4] = [21];
    var first = Link { a[..], null };
    let second = Link { b[..], first };
    first.next .= second;
    let named: const Link& = first;
    await yield_now();
    return await forwarded(Start { named, 5 });
}

fn main() {
    print("local ", local().join());
    global_first.next .= global_second;
    print("global ", walk(global_first, 4).join());
    task_scope(forwarded(Start { global_first, 4 })) { scope =>
        assert(scope.step());
        assert(scope.step());
        untouched.clear();
        scope.cancel(0);
        global_second.next .= null;
    }
    global_values.clear();
    assert(global_values.len == 0);
}
