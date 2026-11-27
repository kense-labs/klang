// error: expected a value of type i64, got bool
decorator Wrong() on async fn { wrap { return false; } }

@Wrong
async fn run() { await yield_now(); return 41; }

fn main() { print(run().join()); }
