// error: another task argument may alias the mutable array
decorator Relay() on async fn { wrap { return await next(); } }

@Relay
async fn read_later(values: i64[>..<]&, view: const i64[:]) -> i64 {
    values.push(41);
    await yield_now();
    return view[0];
}

fn main() {
    var values: i64[>..<] = [6];
    print(read_later(values, values[..]).join());
}
