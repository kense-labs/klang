// error: owned resource handle is already claimed by defer
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn dispose(handle: own Handle);

fn main() {
    using let handle = acquire();
    dispose(handle);
}
