// error: decorator Watch requires signature
decorator Watch() on async fn();

@Watch
async fn run() { await yield_now(); return 41; }

fn main() {}
