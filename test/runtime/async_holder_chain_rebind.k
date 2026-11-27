struct Data { value: const i64[:] }
struct Middle { data: const Data? }
struct Entry { middle: const Middle& }

async fn read(input: Entry) -> i64 {
    await yield_now();
    let data .= input.middle.data;
    if data { return data.value[0]; }
    return 0;
}

fn main() {
    let records: i64[1] = [18];
    let data = Data { records[..] };
    var middle = Middle { data };
    let entry = Entry { middle };
    task_scope(read(entry)) { scope =>
        assert(scope.step());
        print("reader pending");
        middle.data .= null;
        print(scope.join(0));
    }
}
