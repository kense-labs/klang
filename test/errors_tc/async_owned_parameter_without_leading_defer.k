// error: an owned async parameter needs a leading defer to release it before the first poll
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn release(handle: own Handle);

async fn wrong(handle: own Handle) {
    release(handle);
}

fn main() { wrong(acquire()).join(); }
