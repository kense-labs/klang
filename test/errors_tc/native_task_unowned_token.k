// error: native_task needs an owned handle record containing one u64 token
struct Handle { token: u64 }

fn main() {
    let ordinary = Handle { token: 0 };
    native_task<i32>(ordinary).join();
}
