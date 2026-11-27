// error: the async decorator continuation has already been consumed
decorator Predicate() on async fn {
    wrap {
        while await next() { return true; }
        return await next();
    }
}

@Predicate
async fn test() -> bool { return false; }

fn main() { print(test().join()); }
