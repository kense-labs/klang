// error: owned callable captures need read-only references to stable fixed storage
fn main() {
    var slots: i64[..4] = [11, 21];
    let closure = fn[value: const i64& = slots[0]]() -> i64 { value };
    let callable = own_callable<fn[8]() -> i64>(closure);
    slots.clear();
    print(callable());
}
