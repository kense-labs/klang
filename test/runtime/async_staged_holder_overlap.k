struct Link { value: i64, next: const Link? }
var nodes: Link[..5] = [Link { 10, null }, Link { 20, null }, Link { 30, null }];

fn read(input: const Link&, n: i64) -> i64 {
    let next .= input.next;
    if next { return next.value + n; }
    return n;
}

async fn suspended() -> i64 {
    var source: const Link& = nodes[0];
    return read(source, { source .= nodes[1]; await yield_now(7) });
}

fn main() {
    nodes[0].next .= nodes[2];
    task_scope(suspended()) { scope =>
        assert(scope.step());
        nodes[1] = Link { 21, null };
        print("staged argument still reaches the old slot");
        nodes[2] = Link { 31, null };
        print(scope.join(0));
    }
}
