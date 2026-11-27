struct StreamReader { token: u64 }
struct NativeTask { token: u64 }
struct ReadResult { status: i32, error: i32, count: u32 }
struct StreamStats { submitted: u64, queued: u32, reading: u32 }

extern "lib/tcp_fixture" {
    fn validate_capacity() = "k_test_stream_validate_capacity";
    fn open_stream(capacity: u32) -> own StreamReader = "k_test_stream_open";
    fn pump(borrow reader: StreamReader) -> own NativeTask = "k_test_stream_pump";
    fn take(borrow reader: StreamReader, buffer: u8[:]) -> ReadResult = "k_test_stream_take";
    fn stats(borrow reader: StreamReader) -> StreamStats = "k_test_stream_stats";
    fn close_stream(reader: own StreamReader) = "k_test_stream_close";

    fn send_byte(value: i32) = "k_test_send";
    fn send_pattern(count: i32) = "k_test_stream_send_pattern";
    fn end_peer() = "k_test_end_peer";
    fn reset_peer() = "k_test_reset_peer";
    fn replace_provider() = "k_test_stream_replace";
    fn accept_completion() = "k_test_stream_accept_completion";
    fn check_pin(live: i32) = "k_test_stream_pin";
    fn check(status: i32, started: i32, cancelled: i32, unloaded: i32) = "k_test_check";
    fn poll_count() -> i32 = "k_test_poll_count";
    fn wake_idle_peer(value: i32, end: i32) = "k_test_wake_idle_peer";
}

async fn deliver_cancellation() {
    await yield_now();
    accept_completion();
}

fn read_to_end() {
    let reader = open_stream(2);
    defer close_stream(reader);
    var bytes: u8[4];
    assert(take(reader, bytes[..]).status == 0);
    assert(take(reader, bytes[..0]).status == 3);
    task_scope(native_task<ReadResult>(pump(reader))) { scope =>
        assert(scope.step());
        assert(stats(reader).submitted == 1);
        send_byte(11);
        while stats(reader).queued < 1 { assert(scope.step()); }
        assert(stats(reader).submitted == 2);
        send_byte(22);
        while stats(reader).queued < 2 { assert(scope.step()); }
        assert(stats(reader).submitted == 2 && stats(reader).reading == 0);

        send_byte(33);
        assert(scope.step());
        let parked_polls = poll_count();
        for _ in 0..20 { assert(scope.step()); }
        assert(stats(reader).submitted == 2 && stats(reader).queued == 2);
        assert(poll_count() == parked_polls);

        let first = take(reader, bytes[..]);
        assert(first.status == 1 && first.count == 1 && bytes[0] == 11);
        while stats(reader).queued < 2 { assert(scope.wait()); }
        assert(stats(reader).submitted == 3 && stats(reader).reading == 0);

        end_peer();
        let second = take(reader, bytes[..]);
        assert(second.status == 1 && second.count == 1 && bytes[0] == 22);
        let end = scope.join(0);
        assert(end.status == 2 && end.error == 0 && end.count == 0);
        let third = take(reader, bytes[..]);
        assert(third.status == 1 && third.count == 1 && bytes[0] == 33);
        assert(take(reader, bytes[..]).status == 2);
    }
    check(4, 1, 0, 0);
    print("bounded stream paused and resumed in FIFO order through EOF");
}

fn wait_for_socket() {
    let reader = open_stream(2);
    defer close_stream(reader);
    var bytes: u8[4];
    task_scope(native_task<ReadResult>(pump(reader))) { scope =>
        assert(scope.step());
        wake_idle_peer(75, 1);
        assert(scope.wait());
        let data = take(reader, bytes[..]);
        assert(data.status == 1 && data.count == 1 && bytes[0] == 75);
        assert(scope.join(0).status == 2);
        assert(!scope.wait());
    }
    check(4, 1, 0, 0);
    print("idle stream stopped polling and woke for a single TCP payload");
}

fn take_small_slices() {
    let reader = open_stream(2);
    defer close_stream(reader);
    var bytes: u8[37];
    send_pattern(1024);
    task_scope(native_task<ReadResult>(pump(reader))) { scope =>
        var consumed: u32 = 0;
        while consumed < 1024 {
            assert(scope.step());
            let data = take(reader, bytes[..]);
            if data.status == 0 { continue; }
            assert(data.status == 1 && data.count > 0 && data.count <= 37);
            for i in 0..data.count {
                assert(bytes[i] == ((consumed + i) % 251) as u8);
            }
            consumed += data.count;
        }
        end_peer();
        assert(scope.join(0).status == 2);
    }
    check(4, 1, 0, 0);
    print("small consumer slices preserved the complete TCP payload");
}

fn read_to_reset() {
    let reader = open_stream(2);
    defer close_stream(reader);
    var bytes: u8[4];
    task_scope(native_task<ReadResult>(pump(reader))) { scope =>
        assert(scope.step());
        send_byte(44);
        while stats(reader).queued < 1 { assert(scope.step()); }
        reset_peer();
        let error = scope.join(0);
        assert(error.status == 3 && error.error != 0);
        let data = take(reader, bytes[..]);
        assert(data.status == 1 && data.count == 1 && bytes[0] == 44);
        let after = take(reader, bytes[..]);
        assert(after.status == 3 && after.error == error.error && after.count == 0);
    }
    check(4, 1, 0, 0);
    print("stream delivered buffered data before the socket error");
}

fn close_reader_while_receiving() {
    let reader = open_stream(1);
    task_scope(native_task<ReadResult>(pump(reader))) { scope =>
        assert(scope.step());
        while stats(reader).reading == 0 {}
        close_stream(reader);
        let end = scope.join(0);
        assert(end.status == 4 && end.count == 0);
    }
    check(4, 1, 0, 0);
    print("releasing a reader drained its in-flight socket operation");
}

fn cancel_after_replacement() {
    let reader = open_stream(2);
    var bytes: u8[4];
    task_scope(native_task<ReadResult>(pump(reader)), deliver_cancellation()) { scope =>
        assert(scope.step());
        while stats(reader).reading == 0 {}
        let submitted = stats(reader).submitted;
        assert(scope.step());
        replace_provider();
        scope.cancel(0);
        assert(stats(reader).submitted == submitted && stats(reader).reading == 0);
        assert(take(reader, bytes[..]).status == 4);
        check_pin(1);
    }
    close_stream(reader);
    check_pin(0);
    check(6, 1, 1, 0);
    print("cancelled stream kept its retired provider through drain");
}

fn cancel_unstarted() {
    let reader = open_stream(1);
    var bytes: u8[1];
    task_scope(native_task<ReadResult>(pump(reader))) { scope => scope.cancel(0); }
    assert(take(reader, bytes[..]).status == 4 && stats(reader).submitted == 0);
    close_stream(reader);
    check(6, 0, 0, 0);
    print("unstarted stream released its socket");
}

fn main() {
    validate_capacity();
    wait_for_socket();
    read_to_end();
    take_small_slices();
    read_to_reset();
    close_reader_while_receiving();
    cancel_after_replacement();
    cancel_unstarted();
}
