struct Node { data: const i64[:], next: const Node? }

var source: i64[2] = [11, 21];
var nodes: Node[..4] = [Node { source[..], null }, Node { source[1..], null }];

async fn read() -> i64 {
    let saved: const Node& = nodes[0];
    await yield_now();
    let next .= saved.next;
    if next { return next.data[0]; }
    return 0;
}

fn main() {
    nodes[0].next .= nodes[1];
    task_scope(read()) { scope =>
        assert(scope.step());
        print("retained link reaches another slot");
        nodes[1] = Node { source[..], null };
        print(scope.join(0));
    }
}
