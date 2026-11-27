import std;

let fixed_global: i64[2] = [40, 2];
var limited_global: i64[..4] = [4, 5];

fn add(value: const i64&, extra: i64) -> i64 { value + extra }
fn head_add(values: const i64[:], extra: i64) -> i64 { values[0] + extra }
fn pick(values: i64[:], index: i64) -> i64& { return values[index]; }
fn append_and_sum(values: i64[>..]&, value: i64) -> i64 {
    values.push(value);
    return values[0] + values[1];
}

async fn mutable_local() -> i64 {
    await yield_now();
    var local: i64 = 40;
    let ref .= local;
    await yield_now();
    ref += 2;
    return local;
}

async fn borrowed_region(seed: i64) -> i64 {
    let large = [seed; 1024];
    let item .= large[1023];
    var values: i64[>..] = [seed, seed + 1];
    let list .= values;
    let first .= list[0];
    await yield_now();
    list.push(2);
    first += 1;
    return item + list[2] + first;
}

async fn through_loop(count: i64) -> i64 {
    let values: i64[2] = [1, 2];
    let view: const i64& = values[0];
    var sum = 0;
    for index in count {
        sum += view + index;
        await yield_now();
    }
    return sum;
}

async fn select_element() -> i64 {
    var left: i64[2] = [10, 20];
    var right: i64[2] = [100, 200];
    var view = left[..];
    var sum = 0;
    for &element, index in view {
        if index == 0 { view = right[..]; }
        await yield_now();
        element += 1;
        sum += element;
    }
    return sum;
}

async fn saved_arguments() -> i64 {
    let left: i64[2] = [10, 20];
    let right: i64[2] = [100, 200];
    var ref: const i64& = left[0];
    var view = left[..];
    let by_ref = add(ref, { ref .= right[0]; await yield_now(1) });
    let by_slice = head_add(view, { view = right[..]; await yield_now(2) });
    return by_ref + by_slice + ref + view[0];
}

async fn nullable() -> i64 {
    let missing: const i64? = null;
    let global_item: const i64& = fixed_global[0];
    await yield_now();
    assert(missing .== null);
    return global_item + 2;
}

async fn rebind_after_await() -> i64 {
    var data: i64[2] = [10, 40];
    var value .= data[0];
    value .= pick(data[..], await yield_now(1));
    value += 2;
    return value;
}

async fn overwrite_old_loan() -> i64 {
    var value: const i64& = limited_global[0];
    let before = value;
    await yield_now();
    value .= fixed_global[0];
    return before + value;
}

async fn saved_resizable_argument() -> i64 {
    var data: i64[>..] = [40];
    return append_and_sum(data, await yield_now(2));
}

async fn cleanup(run: bool) {
    if run {
        let values: i64[2] = [30, 31];
        let view: const i64& = values[1];
        fn report() { print("cleanup ", view); }
        defer report();
        await yield_now();
        print("completed");
    }
}

fn main() {
    print("local ", mutable_local().join());
    print("region ", borrowed_region(20).join());
    print("loop ", through_loop(3).join());
    print("slice elements ", select_element().join());
    print("arguments ", saved_arguments().join());
    print("optional ", nullable().join());
    print("rebound ", rebind_after_await().join());
    print("overwritten ", overwrite_old_loan().join());
    print("resizable argument ", saved_resizable_argument().join());
    cleanup(true).join();
    task_scope(cleanup(true)) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
}
