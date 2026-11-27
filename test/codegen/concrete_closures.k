struct Box<T> { call: T }

fn make(bias: i64) {
    return fn[bias: i64](x: i64) -> i64 { return bias + x; };
}

fn apply<C>(call: C, x: i64) -> i64 { call(x) }

fn nested<T>(value: T) {
    let outer = fn[value: T]() { fn[value: T]() -> T { value } };
    return outer();
}

fn note(n: i64) -> i64 { print("evaluate ", n); return n; }

fn main() {
    var bias = 3;
    let add = make(bias);
    bias = 80;
    let box = Box { call: add };
    let list = [add, make(5)];
    print(add(4), " ", apply(add, 4), " ", box.call(4), " ", list[1](4), " ", make(6)(4));
    print(nested(12)(), " ", nested(true)());

    var selected = if bias > 0 { make(10) } else { make(20) };
    print(selected(2));
    selected = make(21);
    print(selected(2));

    let result = fn[a: i64 = note(1), b: i64 = note(2)](x: i64) -> i64 { a + b + x }(note(3));
    print(result, " ", fn(x: i64) -> i64 { x + 1 }(8));
    let expand = fn() -> i64 {
        var result = 0;
        const for <N> in [2, 3] { result += N; }
        return result;
    };
    print(expand());
}
