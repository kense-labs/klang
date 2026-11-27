// error: closure captures need 16 bytes but the callable reserves 15
struct Padded { flag: u8, pad, value: i64 }
fn main() {
    let closure = fn[x: Padded = Padded { 1, 3 }]() -> i64 { x.value };
    let too_small = own_callable<fn[15]() -> i64>(closure);
    print(too_small());
}
