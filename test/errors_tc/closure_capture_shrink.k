// error: cannot clear values while read is still used: a reference into it was stored there
fn main() {
    var values: i64[>..] = [];
    values.push(5);
    let view = values[..];
    let read = fn[view: i64[:]]() -> i64 { view[0] };
    values.clear();
    print(read());
}
