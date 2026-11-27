struct Padded { marker: u8, pad, value: i64 }
struct Box { marker: u8, call: fn[16](i64) -> i64 }
struct Wide { values: i64[700] }
enum CapturedState { Ready { items: Padded[..3] }, Empty }

fn adder(value: i64) {
    fn[value: i64](n: i64) -> i64 { value + n }
}

fn choose(add: bool) -> fn[16](i64) -> i64 {
    if add {
        return own_callable<fn[8 + 8](i64) -> i64>(adder(4));
    }
    let affine = fn[scale: i64 = 2, offset: i64 = 3](n: i64) -> i64 { scale * n + offset };
    return own_callable<fn[16](i64) -> i64>(affine);
}

fn forward<T>(x: T) -> T { x }
fn remember<T>(x: T) -> fn[16]() -> T {
    return own_callable<fn[16]() -> T>(fn[value: T = x]() -> T { value });
}
fn square(x: i64) -> i64 { x * x }

var factories: i64 = 0;
fn stateless() {
    factories += 1;
    return fn(n: i64) -> i64 { n + 2 };
}

fn copied_capture() -> i64 {
    var source = adder(9);
    let snapshot: fn[8](i64) -> i64 = own_callable(source);
    source = adder(12);
    let nested: fn[16](i64) -> i64 = own_callable<fn[16](i64) -> i64>(
        fn[apply: fn[8](i64) -> i64 = snapshot](n: i64) -> i64 { apply(apply(n)) });
    return snapshot(1) + source(1) + nested(1);
}

fn reference_capture() -> i64 {
    var number: i64 = 11;
    let escaped_environment = {
        let local = fn[value: const i64& = number](n: i64) -> i64 { value + n };
        own_callable<fn[8](i64) -> i64>(local)
    };
    let copied = forward(escaped_environment);
    number = 20;
    return copied(2);
}

fn wide_capture() -> i64 {
    let callback: fn[5600](i64) -> i64 = own_callable(
        fn[values: Wide = Wide { [7; 700] }](n: i64) -> i64 { values.values[699] + n });
    return forward(callback)(5);
}

fn freeze_state(states: CapturedState[2]) -> fn[100]() -> i64 {
    return own_callable<fn[100]() -> i64>(fn[states: CapturedState[2]]() -> i64 {
        match states[0] { Ready payload => payload.items[0].value, Empty => -1 }
    });
}

fn check_capture_equality() {
    var previous: Padded[..3] = [Padded { 1, 7 }, Padded { 2, 99 }];
    previous.pop();
    let fresh: Padded[..3] = [Padded { 1, 7 }];
    let before: CapturedState = CapturedState.Ready { previous };
    let after: CapturedState = CapturedState.Ready { fresh };
    let old = freeze_state([before, CapturedState.Empty]);
    let current = freeze_state([after, CapturedState.Empty]);
    let empty = freeze_state([CapturedState.Empty, CapturedState.Empty]);
    assert(old() == 7 && current() == 7 && empty() == -1);
    assert(old == current && old != empty);
}

fn map3<C>(apply: C, input: i64[3]) -> i64[3] {
    return [apply(input[0]), apply(input[1]), apply(input[2])];
}

fn filter3<C>(keep: C, input: i64[3]) -> i64[..3] {
    var result: i64[..3] = [];
    for value in input {
        if keep(value) { result.push(value); }
    }
    return result;
}

fn fold<C>(combine: C, input: const i64[:]) -> i64 {
    var total: i64 = 0;
    for value in input { total = combine(total, value); }
    return total;
}

fn sum(a: i64, b: i64) -> i64 { a + b }

fn pipeline() -> i64 {
    let add = adder(4);
    let borrowed: fn&(i64) -> i64 = borrow_callable(add);
    let owned = choose(true);
    let mapped = map3(owned, [1, 2, 3]);
    assert(mapped == map3(borrowed, [1, 2, 3]));
    let pred = fn[minimum: i64 = 5](n: i64) -> bool { n > minimum };
    let keep: fn[8](i64) -> bool = own_callable(pred);
    let borrow_pred: fn&(i64) -> bool = borrow_callable(pred);
    let filtered = filter3(keep, mapped);
    assert(filtered == filter3(borrow_pred, mapped));
    let accum: fn&(i64, i64) -> i64 = borrow_callable(sum);
    let own_accum: fn[0](i64, i64) -> i64 = own_callable(sum);
    let expected = fold(accum, filtered[..]);
    assert(expected == fold(own_accum, filtered[..]));
    return expected;
}

fn main() {
    let alternatives = [choose(true), choose(false)];
    print("selected ", alternatives[0](5), " ", alternatives[1](5));
    print("copied ", copied_capture(), " referenced ", reference_capture());
    let padded = fn[config: Padded = Padded { 1, 13 }](n: i64) -> i64 { n + config.value };
    assert(remember(Padded { 2, 17 })().value == 17 && remember(41)() == 41);
    let packed = Box { 4, own_callable<fn[16](i64) -> i64>(padded) };
    print("packed ", packed.call(1), " wide ", wide_capture());
    let named = own_callable<fn[0](i64) -> i64>(square);
    let empty = own_callable<fn[0](i64) -> i64>(stateless());
    assert(named == forward(named) && named != empty);
    assert(alternatives[0] == forward(alternatives[0]) && alternatives[0] != alternatives[1]);
    check_capture_equality();
    print("stateless ", named(5), " ", empty(5), " ", factories);
    print("pipeline ", pipeline());
}
