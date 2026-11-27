// error: owned resource handle is still held at scope exit
import exception;

struct Handle { token: u64 }
extern fn acquire() -> own Handle;

fn risky() {
    let handle = acquire();
    exception::throw(41);
}

fn main() { exception::try<i64>(risky); }
