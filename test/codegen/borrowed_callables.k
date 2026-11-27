struct Box { apply: fn&(i64) -> i64 }
struct Batch { items: i64[700] }

fn make_add(bias: i64) {
    fn[bias: i64](value: i64) -> i64 { bias + value }
}

fn twice<T>(f: fn&(T) -> T, value: T) -> T { f(f(value)) }

fn triple(value: i64) -> i64 { value * 3 }
fn triple(value: f64) -> f64 { value * 3.0 }

let named: fn&(i64) -> i64 = borrow_callable<fn&(i64) -> i64>(triple);

fn use_layouts(left_first: bool) -> i64 {
    var add = make_add(4);
    let affine = fn[scale: i64 = 2, offset: i64 = 3](value: i64) -> i64 {
        scale * value + offset
    };
    let first: fn&(i64) -> i64 = borrow_callable(add);
    let second: fn&(i64) -> i64 = borrow_callable(affine);
    let choices: (fn&(i64) -> i64)[2] = [first, second];
    let box = Box { choices[if left_first { 0 } else { 1 }] };
    let result = twice(box.apply, 5) + box.apply(1);
    add = make_add(10);
    assert(first(1) == add(1) && first(1) == 11);
    return result;
}

fn higher_order() -> i64 {
    let apply_twice = fn(f: fn&(i64) -> i64, n: i64) -> i64 { f(f(n)) };
    let invoke: fn&(fn&(i64) -> i64, i64) -> i64 = borrow_callable(apply_twice);
    return invoke(named, 5);
}

fn large_values() {
    let add = fn[b: Batch = Batch { [7; 700] }](input: const i64[:], wide: Batch) -> i64, Batch {
        var result = wide;
        result.items[699] += b.items[699];
        return input[0] + b.items[0], result;
    };
    let f: fn&(const i64[:], Batch) -> (i64, Batch) = borrow_callable(add);
    let small, large = f([5, 4], Batch { [9; 700] });
    print(small, " ", large.items[699]);
}

fn main() {
    print(use_layouts(true), " ", use_layouts(false), " ", named(4));
    print(higher_order());
    large_values();
}
