import std;

struct Node { id: i64, me: Node&<u32> }
root struct Table { nodes: Node[..4], other: i64[>..<] }
struct Packet { nodes: Node[..3], tail: i64[>..<] }

var table = Table { [], [6] };
var packet = Packet { [], [9] };
var growing: Node[..] = [..4];

fn node(id: i64) -> Node { Node { id, self } }

async fn scan() -> i64 {
    var sum = 0;
    for &item in table.nodes[1..] {
        await yield_now();
        sum += item.me.id;
    }
    return sum;
}

async fn other() -> i64 {
    table.other.push(7);
    return table.other.len;
}

async fn inspect() -> i64 {
    let held .= packet.nodes[0];
    let view = growing[..];
    defer { print("cleanup ", held.me.id, " ", view[0].me.id); }
    await yield_now();
    return held.me.id + view[0].me.id;
}

async fn read(value: const i64&) -> i64 {
    await yield_now();
    return value;
}

fn replace_dynamic(nodes: Node[..]&, value: i64) { nodes = [node(value)]; }

fn main() {
    table.nodes.push(node(11));
    table.nodes.push(node(21));
    task_scope(scan(), other()) { scope =>
        assert(scope.step());
        table.nodes.push(node(31));
        assert(scope.join(1) == 2);
        print("sampled ", scope.join(0));
    }
    task_scope(other()) { scope =>
        table.nodes = [node({ assert(scope.join(0) == 3); 41 })];
    }
    packet.nodes.push(node(51));
    growing.push(node(61));
    task_scope(inspect(), read(table.nodes[0].id)) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
        packet = Packet { [node(52)], [] };
        replace_dynamic(growing, 62);
        print("scoped ", scope.join(1));
    }
    print("replaced ", packet.nodes[0].me.id, " ", growing[0].me.id);
    print("joined ", inspect().join());
}
