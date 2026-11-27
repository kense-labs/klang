struct View { data: const i64[:] }

var source: i64[2] = [11, 21];
var views: View[..4] = [View { source[..] }, View { source[1..] }];

async fn mutate_other(input: const View&) -> i64 {
    await yield_now();
    print("caller retained another slot");
    views[1] = View { source[..] };
    return input.data[0];
}

async fn parent() -> i64 {
    let other: const View& = views[1];
    let result = await mutate_other(views[0]);
    return result + other.data[0];
}

fn main() { print(parent().join()); }
