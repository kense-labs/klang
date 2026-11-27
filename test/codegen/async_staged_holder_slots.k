struct Link { value: i64, next: const Link? }
struct Holder { first: const Link& }
struct View { data: const i64[:], tag: i64 }

var nodes: Link[..6] = [Link { 10, null }, Link { 20, null }, Link { 30, null }, Link { 40, null }];
var readings: i64 = 0;
var data: i64[2] = [11, 21];
var views: View[..3] = [View { data[..], 1 }, View { data[1..], 2 }];

fn capture(node: const Link&) -> const Link& {
    readings += 1;
    return node;
}

fn combine(first: const Link&, second: i64, third: i64) -> i64 {
    let next .= first.next;
    return first.value + second + third + (if next { next.value } else { 0 });
}

fn read_copy(input: Holder, n: i64) -> i64 { combine(input.first, n, 0) }
fn read_view(input: const View&, n: i64) -> i64 { input.data[0] + input.tag + n }

async fn write_other(also: const Link&) -> i64 {
    await yield_now();
    nodes[1] = Link { 21, null };
    return also.value + 4;
}

async fn staged_reference() -> i64 {
    var source: const Link& = nodes[0];
    return combine(capture(source), { source .= nodes[1]; await write_other(nodes[3]) }, await yield_now(7));
}

async fn staged_copy() -> i64 {
    var source = Holder { nodes[0] };
    return read_copy(source, { source.first .= nodes[1]; await yield_now(5) });
}

async fn direct() -> i64 {
    var source: const View& = views[0];
    return read_view(source, { source .= views[1]; await yield_now(3) });
}

async fn local_storage() -> i64 {
    var cells: Link[..] = [..3];
    cells.append([Link { 5, null }, Link { 6, null }, Link { 7, null }]);
    cells[0].next .= cells[1];
    return combine(cells[0], { cells[2] = Link { 80, null }; await yield_now(2) }, 0);
}

fn main() {
    nodes[0].next .= nodes[2];
    print("reference ", staged_reference().join(), " ", readings, " ", nodes[1].value);
    task_scope(staged_copy()) { scope =>
        assert(scope.step());
        nodes[1] = Link { 22, null };
        print("copy ", scope.join(0));
    }
    task_scope(staged_copy()) { scope =>
        assert(scope.step());
        scope.cancel(0);
        nodes[2] = Link { 31, null };
    }
    task_scope(direct()) { scope =>
        assert(scope.step());
        views[1] = View { data[..], 9 };
        print("direct ", scope.join(0));
    }
    print("local ", local_storage().join(), " ", nodes[2].value);
}
