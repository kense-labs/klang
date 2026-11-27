// error: the async decorator continuation has already been consumed
decorator Twice(early: bool) on async fn {
    wrap {
        if early { await next(); }
        return await next();
    }
}

@Twice(false)
async fn run() -> i64 { return 41; }

fn main() { print(run().join()); }
