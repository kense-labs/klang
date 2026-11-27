// error: returning a reference rooted in owner, which dies with this function
struct Borrowed { value: i64& }

fn escape() {
    var owner = 4;
    let holder = Borrowed { value: owner };
    return [fn[holder: Borrowed]() -> i64 { holder.value }];
}

fn main() { let saved = escape(); print(saved[0]()); }
