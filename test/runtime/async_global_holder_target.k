struct Views { left: const i64[:], right: const i64[:] }

var left: i64[..4] = [11, 12];
var right: i64[..4] = [21, 22];
var later: i64[..4] = [31, 32];
var view = Views { left[..], right[..] };

async fn read(input: const Views&) -> i64 {
    await yield_now();
    return input.left[0] + input.right[0];
}

async fn relay() -> i64 { return await read(view); }

fn main() {
    update(Views { left[..], later[..] });
    task_scope(relay()) { scope =>
        assert(scope.step());
        print("reader pending");
        later.clear();
        print(scope.join(0));
    }
}

fn update(input: Views) { view = input; }
