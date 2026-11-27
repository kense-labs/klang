// error: owned resource handle is still held at scope exit
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn release(handle: own Handle);

async fn misplaced(stop: bool) {
    let handle: own Handle = acquire();
    if stop { return; }
    defer release(handle);
    await yield_now();
}

fn main() { misplaced(true).join(); }
