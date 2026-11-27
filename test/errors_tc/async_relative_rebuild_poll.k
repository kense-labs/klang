// error: a task may use observed while nodes is being rebuilt in place
struct Node { id: i64, me: Node&<u32> }
var nodes: Node[..4] = [Node { 71, self }];
let observed .= nodes[0].id;

async fn inspect() {
    print(observed);
    await yield_now();
}

fn main() {
    task_scope(inspect()) { scope =>
        nodes = [Node { { scope.step(); 91 }, self }];
        scope.join(0);
    }
}
