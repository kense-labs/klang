namespace registry;

decorator Handler(weight: i64) on fn;
decorator Entity(number: i64) on struct;
decorator Category(number: i64) on enum;

decorator Job(delta: i64) on async fn(i64) -> i64 {
    wrap { return (await next()) + delta; }
}

@Job(3)
async fn load(value: i64) -> i64 {
    await yield_now();
    return 2 * value;
}

@Job(4)
async fn queue(value: i64) -> i64 { return 3 * value; }

const ByteWeight: i64 = 4;

@Handler(ByteWeight)
fn dispatch(value: u8) -> i64 { return value + 4; }

@Handler(2)
fn assemble<T>(value: T) -> i64 { return value + 2; }

@Handler(1)
fn uncalled(value: i64) -> i64 { return 1; }

fn ignored() -> i64 { return 99; }

@Entity(7)
struct Payload<T> { value: T }

@Category(5)
enum State { Ready, Waiting }
