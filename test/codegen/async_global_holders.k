struct Views { left: const i64[:], right: const i64[:], extra: i64 }

var left: i64[..4] = [11, 12];
var right: i64[..4] = [21, 22];
var view = Views { left[..], right[..], 1 };
var spare: i64[..4] = [31, 32];

async fn read(input: const Views&) -> i64 {
    defer { print("cleanup ", input.left[0], " ", input.right[0]); }
    await yield_now();
    return input.left[0] + input.right[0] + input.extra;
}

let early = read(view).join();
var later: i64[..] = [..4];

async fn forward() -> i64 {
    let input: const Views& = view;
    await yield_now();
    return await read(input);
}

fn main() {
    print("early ", early);
    later.push(41);
    update(Views { left[..], later[..], 2 });
    task_scope(read(view)) { scope =>
        assert(scope.step());
        spare.clear();
        later[0] = 44;
        view.extra = 3;
        print("scoped ", scope.join(0));
    }
    print("forwarded ", forward().join());
    task_scope(forward()) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    view.right = right[..];
    later.clear();
    print("rebound ", view.right[0]);
}

fn update(input: Views) { view = input; }
