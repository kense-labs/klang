// error: returning a reference rooted in local, which dies with this function
fn forward<T>(x: T) -> T { x }
fn escape() -> fn[8]() -> i64 {
    let local: i64 = 42;
    let closure = fn[value: const i64& = local]() -> i64 { value };
    let callable = own_callable<fn[8]() -> i64>(closure);
    return forward(callable);
}
fn main() { print(escape()()); }
