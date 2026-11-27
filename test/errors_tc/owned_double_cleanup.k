// error: owned resource handle is already claimed by defer
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn release(handle: own Handle);

fn main() {
    let handle = acquire();
    defer release(handle);
    release(handle);
}
