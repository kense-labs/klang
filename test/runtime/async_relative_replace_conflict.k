struct Node { id: i64, me: Node&<u32> }
struct Table { nodes: Node[..4], other: i64 }
var table = Table { [Node { 71, self }], 2 };

async fn observe() {
    let item .= table.nodes[0];
    await yield_now();
    print(item.me.id);
}

fn replace(target: Table&) {
    target = Table { [Node { { print("RHS evaluated"); 91 }, self }], 5 };
}

fn main() {
    task_scope(observe()) { scope =>
        assert(scope.step());
        print("pending");
        replace(table);
    }
}
