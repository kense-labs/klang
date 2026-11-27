import cordis;
import os;

struct Peer { token: u64 }

extern "lib/tcp_fixture" {
    fn peer_pair() -> own Peer = "k_test_app_pair";
    fn peer_socket(borrow peer: Peer) -> own cordis::TcpSocket = "k_test_app_socket";
    fn peer_open(borrow peer: Peer) -> i32 = "k_test_app_socket_open";
    fn peer_send(borrow peer: Peer, byte: i32) = "k_test_app_send";
    fn peer_end(borrow peer: Peer) = "k_test_app_end";
    fn peer_close(peer: own Peer) = "k_test_app_close";
}

fn print_error() {
    var out: u8[4096] = [0; 4096];
    let bytes = cordis::error_json(out[..]);
    assert(bytes > 0 && bytes <= out.len);
    print(out[..bytes]);
}

fn events(expected_count: i64, last: const u8[:]) {
    var out: u8[>..] = [];
    assert(read_file("events.log", out));
    var count: i64 = 0;
    for byte in out { if byte == 10 { count += 1; } }
    assert(count == expected_count && out.len >= last.len);
    assert(out[out.len - last.len..] == last);
}

var completed_responses: i64 = 0;

decorator StreamRoute(provider: const u8[:], lock: const u8[:], capacity: u32)
    on async fn(own cordis::StreamReader) -> i32 {
    wrap {
        let result = await next();
        completed_responses++;
        return result;
    }
}

@StreamRoute("b", "b.lock", 2)
async fn response(reader: own cordis::StreamReader) -> i32 {
    defer cordis::close_reader(reader);
    await yield_now();
    let end = await native_task<cordis::ReadResult>(cordis::pump(reader));
    assert(end.status == 2);
    var bytes: u8[1] = [0];
    let data = cordis::take(reader, bytes[..]);
    assert(data.status == 1 && data.count == 1);
    return bytes[0];
}

fn discover_route() -> StreamRoute {
    const for <Name, Handler, Metadata> in decorated<StreamRoute, namespace ::> {
        assert(Name == "response" && decorator_metadata<StreamRoute>(Handler) == Metadata);
        return Metadata;
    }
    abort("application route missing");
}

fn main() {
    let route = if arg_count() > 1 {
        print("decorated");
        discover_route()
    } else {
        print("explicit");
        StreamRoute { "b", "b.lock", 2 }
    };
    {
        let denied = cordis::open("application manifest.json", "b.lock");
        defer cordis::close(denied);
        assert(cordis::valid(denied) == 0);
        print_error();
    }
    let host = cordis::open("application manifest.json", "a.lock");
    defer cordis::close(host);
    if cordis::valid(host) == 0 { print_error(); abort("Cordis activation failed"); }
    events(2, "start a\n");
    var text: u8[4096] = [0; 4096];
    let initial = cordis::active_graph(host, text[..]);
    assert(initial > 0 && initial <= text.len);
    print(text[..initial]);
    var too_short: u8[1] = [42];
    assert(cordis::active_graph(host, too_short[..]) == initial && too_short[0] == 42);

    {
        let peer = peer_pair();
        defer peer_close(peer);
        let denied = cordis::adopt_tcp(host, peer_socket(peer), 0);
        defer cordis::close_reader(denied);
        assert(cordis::valid(denied) == 0 && peer_open(peer) == 0);
        print_error();
    }
    {
        let peer = peer_pair();
        defer peer_close(peer);
        let denied = cordis::adopt_tcp(host, peer_socket(peer), 2);
        defer cordis::close_reader(denied);
        assert(cordis::valid(denied) == 0 && peer_open(peer) == 0);
        print_error();
    }
    {
        let peer = peer_pair();
        defer peer_close(peer);
        let old = cordis::adopt_tcp(host, peer_socket(peer), 1);
        defer cordis::close_reader(old);
        assert(cordis::valid(old) == 1);
        task_scope(native_task<cordis::ReadResult>(cordis::pump(old))) { scope =>
            assert(scope.step());
            for _ in 0..1000 {
                if cordis::stats(old).reading == 1 { break; }
                sleep_ms(1);
            }
            assert(cordis::stats(old).reading == 1);
            assert(write_file("b.rules", "modified"));
            assert(cordis::select(host, "b", "b.lock") == -1);
            print_error();
            assert(write_file("b.rules", "original"));
            assert(cordis::select(host, "bad", "bad.lock") == -1);
            print_error();
            assert(cordis::active_provider(host, text[..]) == 1 && text[..1] == "a");
            events(4, "stop policy b\n");

            assert(write_file("mutate.flag", "on"));
            assert(cordis::select(host, "b", "b.lock") == -1);
            print_error();
            assert(delete_file("mutate.flag") && write_file("b.rules", "original"));
            assert(cordis::active_provider(host, text[..]) == 1 && text[..1] == "a");
            events(8, "stop policy b\n");

            assert(cordis::select(host, route.provider, route.lock) == 1);
            assert(cordis::select(host, route.provider, route.lock) == 0);
            events(10, "start b\n");
            let selected = cordis::active_graph(host, text[..]);
            assert(selected > 0 && selected <= text.len);
            print(text[..selected]);
            assert(cordis::stream_provider(old, text[..]) == 1 && text[..1] == "a");
            assert(peer_open(peer) == 1);

            let next_peer = peer_pair();
            defer peer_close(next_peer);
            let next_reader = cordis::adopt_tcp(host, peer_socket(next_peer), route.capacity);
            assert(cordis::valid(next_reader) == 1);
            assert(cordis::stream_provider(next_reader, text[..]) == 1 && text[..1] == "b");
            peer_send(next_peer, 77);
            peer_end(next_peer);
            assert(response(next_reader).join() == 77 && peer_open(next_peer) == 0);
            assert(completed_responses == 1);
            {
                let pending_peer = peer_pair();
                defer peer_close(pending_peer);
                let pending_reader = cordis::adopt_tcp(host, peer_socket(pending_peer), route.capacity);
                assert(cordis::valid(pending_reader) == 1);
                task_scope(response(pending_reader)) { request =>
                    assert(request.step());
                    assert(request.step());
                    assert(peer_open(pending_peer) == 1);
                    request.cancel(0);
                    assert(!request.step());
                }
                assert(peer_open(pending_peer) == 0 && completed_responses == 1);
            }
            assert(peer_open(peer) == 1 && cordis::stats(old).reading == 1);
            print("new request used b while a was pending");

            peer_send(peer, 31);
            while cordis::stats(old).queued == 0 { assert(scope.wait()); }
            peer_send(peer, 32);
            for _ in 0..20 { assert(scope.step()); }
            let full = cordis::stats(old);
            assert(full.submitted == 1 && full.queued == 1 && full.reading == 0);
            var bytes: u8[1] = [0];
            assert(cordis::take(old, bytes[..]).count == 1 && bytes[0] == 31);
            while cordis::stats(old).queued == 0 { assert(scope.wait()); }
            assert(cordis::take(old, bytes[..]).count == 1 && bytes[0] == 32);
            assert(scope.step());
            for _ in 0..1000 {
                if cordis::stats(old).reading == 1 { break; }
                sleep_ms(1);
            }
            assert(cordis::stats(old).reading == 1);
            scope.cancel(0);
            assert(!scope.step() && peer_open(peer) == 0);
            events(10, "start b\n");
            print("old request drained after bounded reads");
        }
    }
    events(12, "stop policy a\n");
    print("retired a and its policy after its reader and pump released");
}
