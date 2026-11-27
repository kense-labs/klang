struct Link { value: i64, next: const Link? }
enum Choice { Idle, Active { head: const Link& } }
struct Routes { choices: Choice[2] }

var shared: Link[..5] = [Link { 10, null }, Link { 20, null }, Link { 30, null }];

fn next_value(node: const Link&) -> i64 {
    let next .= node.next;
    if next { return next.value; }
    return 0;
}

async fn read(node: const Link&, turns: i64) -> i64 {
    defer { print("cleanup ", node.value, " ", next_value(node)); }
    for i in turns { await yield_now(); }
    return node.value + next_value(node);
}

async fn copied(input: Routes) -> i64 {
    await yield_now();
    return match input.choices[1] {
        Idle => 0,
        Active item => next_value(item.head)
    };
}

async fn replace_unreached(node: const Link&) -> i64 {
    await yield_now();
    shared[2] = Link { 42, null };
    return next_value(node);
}

async fn retained() -> i64 {
    let view: const Link& = shared[0];
    await yield_now();
    let result = await replace_unreached(view);
    return result + view.value;
}

fn local_scope() {
    var nodes: Link[..] = [..6];
    nodes.append([Link { 1, null }, Link { 2, null }, Link { 3, null }]);
    nodes[0].next .= nodes[1];
    nodes[1].next .= nodes[0];
    let input = Routes { [Choice.Idle, Choice.Active { nodes[0] }] };
    task_scope(read(nodes[0], 3), copied(input)) { scope =>
        assert(scope.step());
        assert(scope.step());
        nodes[2] = Link { 30, null };
        nodes.push(Link { 4, null });
        print("copied ", scope.join(1));
        scope.cancel(0);
        nodes[1] = Link { 22, null };
    }
    assert(nodes[2].value == 30 && nodes.len == 4);
}

fn early_exit(stop: bool) -> i64 {
    task_scope(read(shared[0], 1)) { scope =>
        assert(scope.step());
        if stop { return 91; }
        return scope.join(0);
    }
}

fn larger_cycle() {
    var nodes: Link[..] = [..128];
    for i in 100 { nodes.push(Link { i, null }); }
    for i in 50 { nodes[2 * i].next .= nodes[2 * ((i + 1) % 50)]; }
    task_scope(read(nodes[0], 1)) { scope =>
        assert(scope.step());
        for i in 50 { nodes[2 * i + 1] = Link { 200 + i, null }; }
        assert(scope.join(0) == 2);
    }
    assert(nodes[99].value == 249);
}

fn main() {
    local_scope();
    shared[0].next .= shared[1];
    shared[1].next .= shared[0];
    print("joined ", replace_unreached(shared[0]).join(), " ", shared[2].value);
    task_scope(retained()) { scope =>
        assert(scope.step());
        shared[2] = Link { 31, null };
        print("retained ", scope.join(0), " ", shared[2].value);
    }
    print("early ", early_exit(true));
    shared[1] = Link { 50, null };
    print("released ", next_value(shared[0]));
    larger_cycle();
}
