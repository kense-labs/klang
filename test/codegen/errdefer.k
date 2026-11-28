import exception;

fn log_step(fail: bool) -> i64 {
    errdefer print("err_inner");
    defer print("defer_inner");
    if fail {
        exception::throw(42);
    }
    return 7;
}

fn mid_from(fail: bool) {
    errdefer print("mid_err");
    defer print("mid_defer");
    if fail {
        return 99 from outer_from;
    }
}

fn outer_from(fail: bool) -> i64 {
    errdefer print("outer_err");
    defer print("outer_defer");
    mid_from(fail);
    return 11;
}

fn nested_inner(fail_inner: bool) {
    errdefer print("blk_err");
    defer print("blk_defer");
    if fail_inner {
        return 55 from nested_blocks;
    }
}

fn nested_blocks(fail_inner: bool) -> i64 {
    for _ in 0..1 {
        errdefer print("loop_err");
        defer print("loop_defer");
        nested_inner(fail_inner);
    }
    return 33;
}

fn normal_loop_continue_break() {
    for i in 0..3 {
        errdefer print("never");
        defer print("iter_", i);
        if i == 0 {
            continue;
        }
        if i == 1 {
            break;
        }
    }
}

fn main() {
    var ok_val: i64 = 0;
    fn run_ok() {
        ok_val = log_step(false);
    }
    exception::try<i64>(run_ok) { _ => print("caught"); };
    print("=", ok_val);

    fn run_fail() {
        log_step(true);
    }
    exception::try<i64>(run_fail) { code => print("caught_", code); };

    print("r_ok=", outer_from(false));
    print("r_fail=", outer_from(true));
    print("n_ok=", nested_blocks(false));
    print("n_fail=", nested_blocks(true));
    normal_loop_continue_break();
}
