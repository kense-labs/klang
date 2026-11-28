// error: struct update cannot copy non-fixed field vals
struct Envelope {
    tag: const u8[:],
    vals: i64[],
}
fn main() {
    let e1 = Envelope { tag: "a", vals: [1, 2] };
    let _e2 = Envelope { tag: "b", ..e1 };
}
