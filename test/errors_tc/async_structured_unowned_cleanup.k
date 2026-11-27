// error: owned value handle needs a defer to release it on cancellation
struct Handle { token: u64 }
extern fn open() -> own Handle;
extern fn close(handle: own Handle);

async fn invalid(flag: bool) {
    if flag {
        let handle = open();
        await yield_now();
        close(handle);
    }
}

fn main() { invalid(true).join(); }
