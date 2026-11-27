var order: i64 = 0;

fn mark(value: i64) { order = 10 * order + value; }
fn audit::record(value: i64) { mark(value); }
fn argument() -> i64 { mark(1); return 4; }
fn fallback() -> i64 { mark(2); return 5; }

decorator audit::Outer(enter: i64, leave: i64) on fn(i64, i64) -> (i64, i64) {
    wrap {
        record(enter);
        defer record(leave);
        next()
    }
}

decorator Inner(enter: i64, leave: i64) on fn {
    wrap {
        mark(enter);
        defer mark(leave);
        fn proceed() -> i64, i64 { return next(); }
        return proceed();
    }
}

@audit::Outer(3, 8)
@Inner(4, 7)
fn run(left: i64, right: i64 = fallback()) -> i64, i64 {
    defer mark(6);
    mark(5);
    if left > 0 { return left + right, right - left; }
    return -1, 0;
}

decorator Trace(label: const u8[:]) on fn {
    wrap {
        print("before ", label);
        defer print("after ", label);
        next()
    }
}

@Trace("generic")
fn measure<T, F>(input: T) { return F(input); }

@Trace("void")
fn tick(target: i64&) { target += 2; }

@Trace("reference")
fn get(target: i64&) -> i64& { return target; }

decorator Add(delta: i64) on fn {
    wrap { return next() + delta; }
}

@Add(1)
recursive fn visits(n: i64) -> i64 {
    if n <= 0 { return 0; }
    return visits(n - 1);
}

fn finish() { return 41 from unwind; }

@Add(1)
fn unwind() -> i64 { finish(); return 0; }

fn main() {
    let sum, difference = run(argument());
    print(order, " ", sum, " ", difference, " ", decorator_metadata<audit::Outer>(run).enter);
    let measured = measure(41) { n => n + 1 };
    print(measured);
    var counter = 8;
    tick(counter);
    let alias .= get(counter);
    alias += 4;
    print(counter);
    print(visits(4), " ", unwind());
}
