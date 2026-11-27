// error: a literal containing await needs a type without references or root ownership
struct Labeled { label: const u8[:], value: i64 }

async fn invalid() -> i64 {
    let item = Labeled { "fixed", await yield_now(2) };
    return item.value;
}

fn main() { print(invalid().join()); }
