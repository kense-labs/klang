import std;

struct Node { value: i64, next: Node&<u32>? }
root struct Bank { nodes: Node[..8], side: i64[>..<] }
var bank = Bank { [], [1] };

async fn read(target: const Node?, extra: i64) -> i64 {
    defer {
        if target { print("leaf cleanup ", target.value); }
        else { print("leaf empty"); }
    }
    await yield_now();
    if target { return target.value + extra; }
    return extra;
}

async fn forward(input: const Node&, extra: i64) -> i64 {
    let next .= input.next;
    return await read(next, extra);
}

async fn delay(extra: i64) -> i64 { return await yield_now(extra); }

async fn staged(input: const Node&) -> i64 {
    return await read(input.next, await delay(3));
}

async fn walk(input: const Node[:]) -> i64 {
    var total = 0;
    for &item in input {
        let next .= item.next;
        await yield_now();
        total += item.value;
        if next { assert(next.value < item.value); }
    }
    return total;
}

async fn update(input: Node&) {
    let next .= input.next;
    await yield_now();
    input.value += 1;
    if next { next.value += 2; }
}

async fn local_owner() -> i64 {
    var nodes: Node[>..] = [Node { 30, null }];
    nodes.push(Node { 70, nodes[0] });
    let result = await forward(nodes[1], 8);
    nodes.clear();
    return result;
}

struct Record { value: i64, back: Record&<varint>?, label: u8[] }

fn first(entries: Record[:]) -> Record& {
    for item in entries { return item; }
    abort("empty records");
}

async fn record_score(item: const Record?) -> i64 {
    await yield_now();
    if item { return item.value + item.label.len; }
    return 0;
}

async fn visit(entries: const Record[:]) -> i64 {
    var total = 0;
    for item in entries {
        total += await record_score(item.back);
        total += item.value + item.label.len;
    }
    return total;
}

fn main() {
    bank.nodes.push(Node { 40, null });
    bank.nodes.push(Node { 50, bank.nodes[0] });
    bank.nodes.push(Node { 60, bank.nodes[1] });
    print("direct ", forward(bank.nodes[2], 3).join());
    task_scope(staged(bank.nodes[2])) { scope =>
        assert(scope.step());
        bank.nodes[2].next .= bank.nodes[0];
        print("saved ", scope.join(0));
    }
    bank.nodes[2].next .= bank.nodes[1];

    task_scope(forward(bank.nodes[2], 100), walk(bank.nodes[1..])) { scope =>
        assert(scope.step());
        bank.side.clear();
        bank.side.push(7);
        bank.nodes.push(Node { 70, null });
        scope.cancel(0);
        print("sampled ", scope.join(1));
    }
    update(bank.nodes[2]).join();
    assert(bank.nodes[1].value == 52 && bank.nodes[2].value == 61);
    bank.nodes.clear();
    assert(bank.side[0] == 7 && bank.nodes.len == 0);
    print("local ", local_owner().join());
    task_scope(local_owner()) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    print("empty ", read(null, 5).join());
    var records: Record[>..] = [Record { 10, null, "ten" }];
    records.push(Record { 21, first(records[..]), "twenty-one" });
    print("sequential ", visit(records[..]).join());
}
