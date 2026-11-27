// error: owned value resource needs a defer to release it on cancellation
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn release(handle: own Handle);

async fn wrong() {
    let resource: own Handle = acquire();
    await yield_now();
    release(resource);
}

fn main() { wrong().join(); }
