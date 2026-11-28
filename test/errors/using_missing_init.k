// error: using declaration requires a single initialized let or var
struct Res { id: i64 }
fn dispose(r: Res) {}

fn main() {
    using let a: Res;
}
