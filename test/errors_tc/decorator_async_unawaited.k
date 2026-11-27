// error: an async decorator continuation is awaited as await next()
decorator Unawaited() on async fn { wrap { next().join() } }

@Unawaited
async fn run() -> i64 { return 41; }

fn main() { print(run().join()); }
