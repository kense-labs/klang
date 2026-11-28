async fn fetch(v: i64) -> i64 {
    await yield_now();
    return v * 2;
}

print("start");
await yield_now();
let first = await fetch(21);
print("first=", first);

let blk = async -> i64 {
    let part = await fetch(5);
    return part + 1;
};
let second = await blk;
print("second=", second);

let third = async -> i64 {
    await yield_now();
    return 99;
}.join();
print("third=", third);

async {
    let x = await fetch(3);
    print("bare_async=", x);
}
print("done");
