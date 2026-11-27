// error: ownership of handle differs between control-flow paths
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn release(handle: own Handle);
extern fn choice() -> bool;

fn main() {
    let handle = acquire();
    if choice() { release(handle); }
}
