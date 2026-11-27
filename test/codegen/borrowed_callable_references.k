struct Box { run: fn&(i64) -> i64 }

fn make(value: const i64&) {
    fn[value: const i64&](x: i64) -> i64 { value + x }
}

fn borrow<T>(environment: const T&) -> fn&(i64) -> i64 {
    return borrow_callable<fn&(i64) -> i64>(environment);
}

fn forward<T>(environment: const T&) -> fn&(i64) -> i64 {
    return borrow(environment);
}

fn replace<T>(slot: T&, fresh: T) { slot = fresh; }
fn apply<F>(callback: F, x: i64) -> i64 { callback(x) }

fn exercise(original: const i64&) -> i64 {
    var alternate: i64[1] = [7];
    let source = make(original);
    let alias .= source;
    let box = Box { forward(alias) };
    let wrapper = fn[callback: fn&(i64) -> i64 = box.run](x: i64) -> i64 { callback(x) };
    let invoke: fn&(i64) -> i64 = borrow_callable(wrapper);
    assert(apply(invoke, 1) == original + 1);
    assert(invoke({ replace(alias, make(alternate[0])); 2 }) == 9);
    alternate[0] = 11;
    return apply(invoke, 3);
}

var global_source: i64 = 7;
let global_environment = make(global_source);
let global_callback: fn&(i64) -> i64 = borrow_callable(global_environment);

fn main() {
    var original: i64 = 4;
    print("local ", exercise(original));

    // A concrete closure in separate storage can still borrow a live
    // grow-only element, even when its nominal shape also has erased uses.
    var growing: i64[>..] = [];
    growing.push(21);
    let concrete = make(growing[0]);
    print("concrete ", concrete(1));

    global_source = 14;
    print("global ", global_callback(1));
}
