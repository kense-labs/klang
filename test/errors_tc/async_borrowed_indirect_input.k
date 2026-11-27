// error: borrowed task input input needs a stable layout within one backing owner
struct Holder { external: i64& }

async fn read(input: Holder&) -> i64 {
    await yield_now();
    return input.external;
}

fn main() {}
