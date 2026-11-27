import std;

async fn child(value: i64) -> i64 {
    defer { print("child cleanup ", value); }
    await yield_now();
    let replacement: i64[600] = [value + 1; 600];
    return replacement[599];
}

async fn retire(run: bool) -> i64 {
    var total = 10;
    var result = 80;
    fn report() { print("owner cleanup ", total); }
    defer report();
    if run {
        var scratch: u8[>..] = [];
        scratch.push(5);
        let view = scratch[..];
        let large: i64[600] = [6; 600];
        let input = view[0] + large[599];
        let answer = await child(input);
        result = answer;
        total += answer;
    }
    return result;
}

async fn repeat_work(count: i64) -> i64 {
    var total = 0;
    for index in count {
        let large: i64[600] = [index + 2; 600];
        let value = await yield_now(large[599]);
        total += value;
    }
    return total;
}

async fn branches(wait_here: bool) -> i64 {
    var data: u8[>..] = [];
    data.push(9);
    let view = data[..];
    if wait_here {
        await yield_now();
        return 1;
    } else {
        return view[0];
    }
}

fn main() {
    print("joined ", retire(true).join());
    print("skipped ", retire(false).join());
    task_scope(retire(true)) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    print("repeated ", repeat_work(4).join());
    print("branches ", branches(false).join(), " ", branches(true).join());
}
