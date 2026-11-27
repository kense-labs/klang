import std;

async fn live(input: const i64[>..<]&) -> i64 {
    var total = 0;
    for value in input {
        await yield_now();
        total += value;
    }
    return total;
}

async fn sampled(left: const i64[>..<]&, right: const i64[>..<]&) -> i64 {
    var alias .= left;
    var total = 0;
    for value in alias[1..] {
        await yield_now();
        alias .= right;
        total += value;
    }
    return total;
}

async fn sequential(left: const u8[][>..]&, right: const u8[][>..]&) -> i64 {
    var alias .= left;
    var total = 0;
    for value, index in alias {
        await yield_now();
        total += value.len;
        if index == 0 { alias .= right; }
    }
    return total;
}

async fn optional(input: const i64[>..<]?) -> i64 {
    await yield_now();
    if input { return input.len; }
    return 0;
}

async fn relay(input: const i64[>..<]&) -> i64 {
    return await live(input);
}

async fn local() -> i64 {
    var values: i64[>..<] = [3, 4];
    defer { print("cleanup ", values.len); }
    let result = await relay(values);
    values.clear();
    return result;
}

async fn static_limit(values: const i64[..4]&) -> i64 {
    var total = 0;
    for value in values[1..] {
        await yield_now();
        total += value;
    }
    return total;
}

async fn runtime_limit(values: const i64[..]&) -> i64 {
    await yield_now();
    return values[0] + values.len + values.cap;
}

struct Node { value: i64, previous: Node&<u32>? }
root struct Bank { nodes: Node[>..], spare: i64[>..<] }
var bank = Bank { [Node { 41, null }], [9] };

async fn linked(values: const Node[>..]&) -> i64 {
    let old = values[..];
    await yield_now();
    let next .= values[1].previous;
    assert(next);
    return old.len * 100 + values.len * 10 + next.value;
}

fn main() {
    var a: i64[>..<] = [2, 3];
    var b: i64[>..<] = [80, 81, 82];
    task_scope(live(a)) { scope =>
        assert(scope.step());
        a.push(5);
        print("live ", scope.join(0));
    }
    print("sampled ", sampled(a, b).join());
    print("optional ", optional(a).join(), " ", optional(null).join());
    a.clear();

    var left: u8[][>..] = ["start", "old"];
    var right: u8[][>..] = ["x", "longer", "tail"];
    print("sequential ", sequential(left, right).join());

    var fixed: i64[..4] = [6, 9];
    var bounded: i64[..] = [..5];
    bounded.push(24);
    print("limited ", static_limit(fixed).join(), " ", runtime_limit(bounded).join());

    task_scope(linked(bank.nodes)) { scope =>
        assert(scope.step());
        bank.nodes.push(Node { 43, bank.nodes[0] });
        bank.spare.clear();
        print("linked ", scope.join(0));
    }
    assert(bank.spare.len == 0);

    print("local ", local().join());
    task_scope(local()) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
}
