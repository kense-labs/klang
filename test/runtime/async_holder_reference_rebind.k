struct Window { value: const i64?, rows: const i64[:] }

async fn read(input: const Window&) -> i64 {
    await yield_now();
    return input.rows[0];
}

fn reset(input: Window&) { input.value .= null; }

fn main() {
    let value: i64 = 2;
    var rows: i64[..4] = [4, 6];
    var input = Window { value, rows[..] };
    task_scope(read(input)) { scope =>
        assert(scope.step());
        print("reader pending");
        reset(input);
        print(scope.join(0));
    }
}
