// error: a native task needs a plain C-compatible result
var pool: i64[>..] = [3];
struct Handle { token: u64 }
extern fn receive() -> own Handle;

fn main() {
    print(native_task<u32 in pool>(receive()).join());
}
