// error: owned resource handle is still held at scope exit
struct Handle { token: u64 }
extern fn acquire() -> own Handle;
extern fn read(borrow handle: Handle) -> i32;

fn main() {
    let handle = acquire();
    print(read(handle));
}
