// A block's compile-time parameter has no runtime slot. The two value
// parameters still evaluate once in order and bind to their own names.
fn twice<Operation>(n: i64) -> i64 { Operation(n) * 2 }

fn lend<Body>(bias: i64) {
    var calls = 0;
    fn next() -> i64 {
        calls++;
        return calls;
    }
    fn add(n: i64) -> i64 { return bias + calls + n; }
    Body(next(), add, next());
}

fn forward<Operation>() {
    fn visit<Body>() { Body(Operation); }
    visit() { op => op(42); };
}

fn main() {
    let operation = "outside";
    lend(40) { first, operation, second =>
        print(first, " ", operation(first), " ", second, " ", twice(second, operation));
    };
    forward() { n => print(n, " ", operation); };
}
