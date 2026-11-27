// error: borrowed callable captures need read-only references to stable fixed storage
fn read(value: const i64&) { fn[value: const i64&]() -> i64 { value } }
fn replace<T>(slot: T&, fresh: T) { slot = fresh; }

fn main() {
    var number: i64 = 2;
    var growing: i64[>..] = [];
    growing.push(7);
    let source = read(number);
    let alias .= source;
    let callback: fn&() -> i64 = borrow_callable(source);
    replace(alias, read(growing[0]));
    growing.clear();
    print(callback());
}
