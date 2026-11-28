struct Guard { name: const u8[:] }
fn dispose(g: Guard) { print("close=", g.name); }

async fn compute(n: i64) -> i64 {
    await yield_now();
    return n + 10;
}

async fn main() {
    using let g = Guard { name: "main" };
    defer print("exit_main");
    await yield_now();
    let a = await compute(5);
    let b = await async -> i64 {
        let sub = await compute(20);
        return sub * 2;
    };
    print("a=", a, " b=", b);
}
