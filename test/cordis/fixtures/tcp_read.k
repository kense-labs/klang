struct NativeHandle { token: u64 }
struct ReadResult { status: i32, value: i32, error: i32 }

extern "lib/tcp_fixture" {
    fn receive() -> own NativeHandle = "k_test_receive";
    fn end_peer() = "k_test_end_peer";
    fn reset_peer() = "k_test_reset_peer";
    fn replace_provider() = "k_test_replace_provider";
    fn accept_completion() = "k_test_accept_completion";
    fn check(status: i32, started: i32, cancelled: i32, unloaded: i32) = "k_test_check";
    fn wake_idle_peer(value: i32, end: i32) = "k_test_wake_idle_peer";
}

async fn read(enabled: bool) -> ReadResult {
    await yield_now();
    if enabled {
        let result = await native_task<ReadResult>(receive());
        return result;
    }
    return ReadResult { -1, 0, 0 };
}

async fn deliver_cancellation() {
    await yield_now();
    await yield_now();
    accept_completion();
}

fn main() {
    assert(read(false).join().status == -1);
    task_scope(read(true)) { scope =>
        assert(scope.step());
        assert(scope.step());
        wake_idle_peer(73, 0);
        let result = scope.join(0);
        assert(result.status == 0 && result.value == 73 && result.error == 0);
    }
    check(4, 1, 0, 0);
    print("read byte");

    let end = native_task<ReadResult>(receive());
    end_peer();
    let eof = end.join();
    assert(eof.status == 1 && eof.error == 0);
    check(4, 1, 0, 0);
    print("read end of stream");

    task_scope(read(true)) { scope =>
        assert(scope.step());
        assert(scope.step());
        reset_peer();
        let result = scope.join(0);
        assert(result.status == 2 && result.error != 0);
    }
    check(4, 1, 0, 0);
    print("read socket error");

    task_scope(read(true), deliver_cancellation()) { scope =>
        assert(scope.step());
        assert(scope.step());
        assert(scope.step());
        replace_provider();
        scope.cancel(0);
    }
    check(6, 1, 1, 1);
    print("cancelled and retired provider after drain");

    let unstarted = native_task<ReadResult>(receive());
    task_scope(unstarted) { scope => scope.cancel(0); }
    check(6, 0, 0, 0);
    print("released unstarted native operation");

    task_scope(read(true)) { scope =>
        assert(scope.step());
        assert(scope.step());
    }
    check(6, 1, 1, 0);
    print("released pending operation on scope exit");
}
