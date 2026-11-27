struct Node { value: i64, next: Node&<u32>? }
var nodes: Node[..4] = [Node { 41, null }];

async fn inner(target: const Node?) {
    await yield_now();
    if target { print(target.value); }
}

async fn outer(input: const Node&) {
    await inner(input.next);
}

fn shrink(owner: Node[..4]&) { owner.clear(); }

fn main() {
    nodes.push(Node { 42, nodes[0] });
    task_scope(outer(nodes[1])) { scope =>
        assert(scope.step());
        print("pending");
        shrink(nodes);
    }
}
