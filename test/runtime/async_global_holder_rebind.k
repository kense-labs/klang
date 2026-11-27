struct Views { rows: const i64[:] }

var left: i64[..4] = [11, 12];
var right: i64[..4] = [21, 22];
var view = Views { left[..] };

async fn read(input: const Views&) -> i64 {
    await yield_now();
    return input.rows[0];
}

fn replace() { view.rows = right[..]; }

fn main() {
    task_scope(read(view)) { scope =>
        assert(scope.step());
        print("reader pending");
        replace();
        print(scope.join(0));
    }
}
