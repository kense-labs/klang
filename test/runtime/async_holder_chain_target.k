struct Data { values: const i64[:] }
struct Middle { data: const Data& }
struct Entry { middle: const Middle& }

var values: i64[..4] = [1, 2];
var data = Data { values[..] };
var middle = Middle { data };
var entry = Entry { middle };

async fn read(input: const Entry&) -> i64 {
    await yield_now();
    return input.middle.data.values[0];
}

async fn forward() -> i64 { return await read(entry); }

fn main() {
    task_scope(forward()) { scope =>
        assert(scope.step());
        print("reader pending");
        values.clear();
        print(scope.join(0));
    }
}
