import std;

var shared: i64[>..<] = [1, 2];
var words: u8[][>..] = ["one", "two"];

async fn observe() -> i64 {
    let view = shared[..];
    defer { print("cleanup ", view[0]); }
    await yield_now();
    return view[0] + view.len;
}

async fn read(value: const i64&) -> i64 {
    await yield_now();
    return value;
}

async fn append() {
    await yield_now();
    shared.push(3);
    words.push("tail");
}

async fn through_child() -> i64 { return await read(shared[0]); }

async fn sum_range() -> i64 {
    var sum = 0;
    for value in shared[1..] {
        await yield_now();
        sum += value;
    }
    return sum;
}

async fn walk_words() -> i64 {
    var bytes = 0;
    for word in words {
        bytes += word.len;
        await yield_now();
    }
    return bytes;
}

fn main() {
    print("joined ", read(shared[0]).join());
    task_scope(observe(), sum_range(), walk_words(), append()) { scope =>
        assert(scope.step());
        assert(scope.step());
        assert(scope.step());
        scope.join(3);
        print("observed ", scope.join(0));
        print("range ", scope.join(1));
        print("sequence ", scope.join(2));
        shared.clear();
        words.clear();
    }
    shared.push(5);
    task_scope(observe(), through_child()) { scope =>
        assert(scope.step());
        scope.cancel(0);
        print("child ", scope.join(1));
        shared.clear();
    }
    var data: i64[>..<] = [41, 42];
    var unrelated: i64[>..<] = [9];
    task_scope(read(data[0]), read(data[1])) { scope =>
        data.push(43);
        unrelated.clear();
        assert(scope.join(0) == 41);
        scope.cancel(1);
        data.clear();
    }
    assert(data.len == 0 && unrelated.len == 0);
    data.push(7);
    task_scope(read(data[0])) { scope => assert(!scope.finished(0)); }
    data.clear();
    print("released ", data.len);
}
