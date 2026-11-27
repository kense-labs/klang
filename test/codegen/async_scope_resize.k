async fn edit(values: i64[>..<]&, fixed: i64[..4]&, source: i64) -> i64 {
    defer {
        values.resize(1);
        fixed.resize(1);
        print("cleanup ", values[0], " ", fixed[0]);
    }
    await yield_now();
    values.push(source);
    fixed.push(source + 1);
    return values.pop() + fixed.pop();
}

async fn relay(values: i64[>..<]&, fixed: i64[..4]&, source: const i64[>..]&) -> i64 {
    return await edit(values, fixed, source[0]);
}

async fn append(values: i64[>..]&) {
    await yield_now();
    values.clear();
    values.push(19);
}

root struct Work { values: i64[>..<], labels: i64[>..<] }

fn run(complete: bool) {
    var work = Work { [12, 13], [3] };
    var fixed: i64[..4] = [5, 6];
    var source: i64[>..] = [10];
    var peer: i64[>..] = [2];
    fn read_label() -> i64 { return work.labels[0]; }

    task_scope(relay(work.values, fixed, source), append(peer)) { scope =>
        assert(read_label() == 3);
        work.labels.push(8);
        assert(scope.step());
        if complete {
            print("joined ", scope.join(0));
        } else {
            scope.cancel(0);
        }
        assert(work.values.len == 1 && fixed.len == 1);
        work.values.push(work.labels.pop());
        fixed.push(11);
        scope.join(1);
        assert(peer.len == 1 && peer[0] == 19);
    }
    print("reused ", work.values[1], " ", fixed[1]);
}

fn leave() -> i64 {
    var values: i64[>..<] = [22, 23];
    var fixed: i64[..4] = [41, 42];
    var source: i64[>..] = [9];
    task_scope(relay(values, fixed, source)) { scope =>
        assert(scope.step());
        return 7;
    }
}

fn main() {
    run(true);
    run(false);
    print("left ", leave());

    var unused: i64[>..] = [3];
    task_scope(append(unused)) { scope => scope.cancel(0); }
    print("unstarted ", unused[0]);
}
