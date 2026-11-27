// error: has already moved
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn release(handle: own Handle);

decorator Twice() on fn { wrap { next(); next(); } }
@Twice
fn consume(handle: own Handle) { release(handle); }
fn main() { consume(acquire()); }
