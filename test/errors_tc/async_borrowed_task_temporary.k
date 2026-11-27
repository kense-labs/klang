// error: a borrowed task argument needs a stable storage owner
struct Cell { value: i64 }
fn cell() -> Cell { Cell { 41 } }
async fn read(value: const i64&) -> i64 {
    await yield_now();
    return value;
}

fn main() { print(read(cell().value).join()); }
