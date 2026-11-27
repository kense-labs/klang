// error: a loop can consume the same async decorator continuation more than once
decorator Repeat(count: i64) on async fn {
    wrap {
        for index in count {
            print(index, " ", await next());
        }
        return 0;
    }
}

@Repeat(2)
async fn run() -> i64 { return 41; }

fn main() { print(run().join()); }
