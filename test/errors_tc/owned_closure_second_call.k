// error: owned resource run has already moved
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn release(handle: own Handle);

fn main() {
    let handle = acquire();
    let run = fn[handle: own Handle]() { release(handle); };
    run();
    run();
}
