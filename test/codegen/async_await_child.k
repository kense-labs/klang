import std;

fn note(n: i32) -> i32 { print("capture ", n); n }

async fn leaf(x: i32) -> i32 {
    print("leaf starts");
    await yield_now();
    print("leaf resumes");
    return x + 1;
}

async fn middle(x: i32) -> i64 {
    print("middle starts");
    let scratch = 2;
    let saved: i64 = 100 + scratch;
    let child = leaf(note(x));
    let value: i64 = await child;
    print("middle resumes");
    return saved + value;
}

async fn outer() -> i64 {
    print("outer starts");
    let value = await middle(6);
    print("outer resumes");
    return value + 10;
}

async fn empty() { print("void child"); }

async fn consume() {
    await empty();
    print("void parent");
}

fn main() {
    let root = outer();
    print("root constructed");
    print(root.join());
    consume().join();
}
