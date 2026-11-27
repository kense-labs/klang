// Taking an inlined boolean branch must run cleanup even when its scope
// used no data-stack storage. Exercise explicit returns and tail values.
var cleanups = 0;

inline fn ready(value: i64) -> bool {
    defer cleanups++;
    if value == 1 { return true; }
    value > 0
}

fn main() {
    if ready(1) { print("return ", cleanups); }
    if !ready(0) { print("false ", cleanups); }
    if ready(2) && !ready(-1) { print("chain ", cleanups); }
    var visits = 0;
    while ready(2 - visits) {
        visits++;
        print("loop ", cleanups);
    }
    assert(ready(1));
    print("done ", cleanups);
}
