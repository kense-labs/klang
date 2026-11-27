import std;

struct Hybrid { prefix: i64[..4], tail: i64[>..<] }
struct Packed { label: u8[], capacity: i64[..4], entries: i64[>..<] }
struct Fixed { buffer: i64[..2], stamp: i64 }
var frame: Hybrid = Hybrid { [10, 20], [1, 2] };
var packed: Packed = Packed { "hello", [7, 8], [9, 10] };
var runtime: i64[..] = [..6];
var fixed: i64[..4] = [41, 42];

async fn read(value: const i64&) -> i64 {
    defer { print("read ", value); }
    await yield_now();
    return value;
}

async fn observe() -> i64 {
    let prefix = frame.prefix[..];
    let tail = frame.tail[..];
    let cap = packed.capacity[..];
    await yield_now();
    return prefix[1] + tail[0] + cap[0];
}

async fn count_tail() -> i64 {
    var sum = 0;
    for value in frame.tail[1..] {
        await yield_now();
        sum += value;
    }
    return sum;
}

async fn local_lender() -> i64 {
    var local = Fixed { [25], 1 };
    task_scope(read(local.buffer[0])) { scope => assert(scope.join(0) == 25); }
    local = Fixed { [17], 2 };
    await yield_now();
    return local.buffer[0] + local.stamp;
}

fn main() {
    runtime.push(21);
    task_scope(observe(), count_tail(), read(runtime[0]), read(fixed[0])) { scope =>
        assert(scope.step());
        assert(scope.step());
        fixed.push(43);
        frame.prefix.push(30);
        frame.tail.push(3);
        print("fields ", scope.join(0));
        print("tail ", scope.join(1));
        scope.cancel(2);
        print("fixed ", scope.join(3));
        runtime.clear();
        frame = Hybrid { [5], [] };
        packed.capacity.clear();
        fixed = [13];
    }
    print("released ", frame.prefix[0], " ", packed.capacity.len, " ", runtime.len, " ", fixed[0]);
    print("local ", local_lender().join());
}
