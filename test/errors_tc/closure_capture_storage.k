// error: closure capture values must have a fixed-size type
fn main() {
    var values: i64[>..] = [];
    values.push(5);
    let closure = fn[values: i64[>..]]() -> i64 { values[0] };
    print(closure());
}
