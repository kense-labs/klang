// error: cannot call observe in the value assigned to packet: observe uses observed, which may refer to packet
struct Node { id: i64, me: Node&<u32> }
struct Packet { nodes: Node[..3], tail: i64[>..<] }
var packet = Packet { [Node { 71, self }], [4] };
let observed .= packet;

async fn inspect() { print(observed.nodes[0].id); await yield_now(); }

fn observe() -> i64 {
    inspect().join();
    return 91;
}

fn main() {
    packet = Packet { [Node { observe(), self }], [] };
}
