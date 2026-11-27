struct Views { left: const i64[:], right: const i64[:] }

async fn read(input: const Views&, extra: i64) -> i64 {
    await yield_now();
    return input.left[0] + input.right[0] + extra;
}

fn main() {
    var left: i64[..4] = [11, 12];
    var right: i64[..4] = [21, 22];
    var later: i64[..4] = [31, 32];
    var input = Views { left[..], right[..] };
    task_scope(read(input, { input.right = later[..]; 2 })) { scope =>
        assert(scope.step());
        print("reader pending");
        later.clear();
        print(scope.join(0));
    }
}
