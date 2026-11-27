struct Node { view: const i64[:], next: const Node? }

async fn read(input: const Node&) -> i64 {
    await yield_now();
    let next .= input.next;
    if next { return next.view[0]; }
    return 0;
}

fn main() {
    let source: i64[2] = [11, 21];
    var nodes: Node[..4] = [Node { source[..], null }, Node { source[1..], null }];
    nodes[0].next .= nodes[1];
    task_scope(read(nodes[0])) { scope =>
        assert(scope.step());
        print("indirect slot borrowed");
        nodes[1] = Node { source[..], null };
        print(scope.join(0));
    }
}
