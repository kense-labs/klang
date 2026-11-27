// error: returning a reference rooted in owner, which dies with the call of lend
var fallback: i64 = 0;

fn lend<Body>() {
    var owner = 17;
    fn borrow() -> i64& { return owner; }
    Body(borrow);
}

fn escape() -> i64& {
    lend() { borrow => return borrow(); };
    return fallback;
}

fn main() { print(escape()); }
