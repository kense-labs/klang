import cordis;

struct Peer { token: u64 }

extern "lib/tcp_fixture" {
    fn peer_pair() -> own Peer = "k_test_app_pair";
    fn peer_socket(borrow peer: Peer) -> own cordis::TcpSocket = "k_test_app_socket";
    fn peer_send(borrow peer: Peer, byte: i32) = "k_test_app_send";
    fn peer_end(borrow peer: Peer) = "k_test_app_end";
    fn peer_close(peer: own Peer) = "k_test_app_close";
}

fn receive(reader: own cordis::StreamReader) -> u8[3] {
    defer cordis::close_reader(reader);
    var input: u8[3] = [0; 3];
    task_scope(native_task<cordis::ReadResult>(cordis::pump(reader))) { scope =>
        var used: i64 = 0;
        while used < input.len {
            let chunk = cordis::take(reader, input[used..]);
            assert(chunk.status == 0 || chunk.status == 1);
            if chunk.count > 0 { used += chunk.count; }
            else { scope.wait(); }
        }
        assert(scope.join(0).status == 2);
    }
    return input;
}

fn main() {
    let host = cordis::open("providers.json", "transport.lock");
    defer cordis::close(host);
    assert(cordis::valid(host) == 1);
    let peer = peer_pair();
    defer peer_close(peer);
    let reader = cordis::adopt_tcp(host, peer_socket(peer), 2);
    assert(cordis::valid(reader) == 1);
    for byte in 1..4 { peer_send(peer, byte as i32); }
    peer_end(peer);
    let data = receive(reader);
    let tensor: f32[3] = [data[0], data[1], data[2]];
    let request = cordis::infer(host, tensor[..]);
    defer cordis::close_inference(request);
    assert(cordis::valid(request) == 1);
    let result = native_task<cordis::InferenceResult>(cordis::inference_task(request)).join();
    assert(result.status == 0 && result.written == 2);
    var output: f32[2] = [0.0; 2];
    assert(cordis::inference_output(request, output[..]) == 2);
    assert(output[0] == 2.0 && output[1] == 8.0);
    print("loopback payload inferred through a locked dependency");
}
