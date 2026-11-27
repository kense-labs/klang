import std;

async fn work(n: i64) -> i64 {
    print("start ", n);
    await yield_now();
    print("finish ", n);
    return n * 10;
}

fn early() -> i64 {
    task_scope(work(4)) { scope =>
        assert(scope.step());
        defer { print("leaving early"); }
        return 77;
    }
}

fn main() {
    task_scope(work(1), work(2), work(3)) { scope =>
        print("scheduled");
        scope.cancel(2);
        scope.cancel(0);
        print("joined ", scope.join(1));
        assert(!scope.step());
    }
    print(early());
    task_scope(work(5)) {
        print("leaving without starting");
    }
    let value = task_scope(work(6)) {
        assert(!it.finished(0));
        let slice: i32 = 3;
        while it.wait_for(slice) {}
        assert(it.finished(0));
        it.join(0)
    };
    print("joined ", value);
}
