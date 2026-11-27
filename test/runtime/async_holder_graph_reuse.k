struct Link { value: i64, next: const Link? }

async fn read(node: const Link&) {
    defer {
        let next .= node.next;
        if next { print(next.value); }
    }
    await yield_now();
}

fn main() {
    var nodes: Link[..4] = [Link { 11, null }, Link { 21, null }];
    nodes[0].next .= nodes[1];
    nodes.resize(1);
    task_scope(read(nodes[0])) { scope =>
        assert(scope.step());
        print("cleanup still reaches the popped slot");
        nodes.push(Link { 31, null });
        scope.cancel(0);
    }
}
