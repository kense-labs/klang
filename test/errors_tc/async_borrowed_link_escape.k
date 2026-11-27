// error: which does not outlive the destination
struct Node { value: i64, next: Node&<u32>? }
var escaped: const Node? = null;
var nodes: Node[>..] = [Node { 41, null }];

async fn publish(input: const Node&) {
    let next .= input.next;
    await yield_now();
    escaped .= next;
}

fn main() {
    nodes.push(Node { 42, nodes[0] });
    publish(nodes[1]).join();
}
