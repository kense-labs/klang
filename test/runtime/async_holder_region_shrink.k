struct View { data: const i64[:] }

async fn read(input: const View&) -> i64 {
    await yield_now();
    return input.data[0];
}

fn main() {
    let source: i64[2] = [11, 21];
    var records: View[>..<] = [View { source[..] }];
    task_scope(read(records[0])) { scope =>
        assert(scope.step());
        print("reader pending");
        records.clear();
        print(scope.join(0));
    }
}
