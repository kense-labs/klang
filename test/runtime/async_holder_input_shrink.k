struct Views { left: const i64[:], right: const i64[:] }

async fn read(input: Views) -> i64 {
    await yield_now();
    return input.left[0] + input.right[0];
}

fn main() {
    var left: i64[..4] = [11, 12];
    var right: i64[..4] = [21, 22];
    task_scope(read(Views { left[..], right[..] })) { scope =>
        assert(scope.step());
        print("reader pending");
        right.clear();
        print(scope.join(0));
    }
}
