// error: an async decorator continuation is awaited as await next()
decorator Explicit() on async fn { wrap { return await next(8); } }

@Explicit
async fn run(value: i64) -> i64 { return value; }

fn main() { print(run(1).join()); }
