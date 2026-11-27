// error: an owned value must be moved or passed to a foreign borrow
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn release(handle: own Handle);

fn main() {
    let handle = acquire();
    let borrowed .= handle;
    release(handle);
    print(borrowed.token);
}
