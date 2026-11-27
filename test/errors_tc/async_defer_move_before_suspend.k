// error: owned resource resource is already claimed by defer
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn release(handle: own Handle);

async fn wrong() {
    let resource: own Handle = acquire();
    defer release(resource);
    release(resource);
    await yield_now();
}

fn main() { wrong().join(); }
