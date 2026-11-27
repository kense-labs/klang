struct Borrowed { value: i64& }
struct Box<T> { get: T }

fn retain(a: i64&) {
    let holder = Borrowed { value: a };
    return fn[holder: Borrowed]() -> i64& { return holder.value; };
}

fn main() {
    var owner = 4;
    let box = Box { get: retain(owner) };
    owner = 6;
    var through .= box.get();
    through += 3;
    print(through, " ", owner);
}
