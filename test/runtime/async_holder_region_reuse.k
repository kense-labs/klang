struct View { data: const i64[:] }

async fn read(input: const View&) -> i64 {
    await yield_now();
    return input.data[0];
}

fn main() {
    let source: i64[2] = [11, 21];
    var records: View[..3] = [View { source[..] }];
    let saved: const View& = records[0];
    records.clear();
    task_scope(read(saved)) { scope =>
        assert(scope.step());
        print("popped slot still borrowed");
        records.push(View { source[1..] });
        print(scope.join(0));
    }
}
