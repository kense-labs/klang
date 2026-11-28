enum Mode { Fast, Exact }

struct Rect {
    w: i64 = 10,
    h: i64 = 20,
}

fn pack(a: i64, b: i64 = 2, c: i64 = 3, d: i64 = 4) -> i64 {
    return a * 1000 + b * 100 + c * 10 + d;
}

fn scale(r: Rect, sx: i64 = 1, sy: i64 = 1, mode: Mode = .Fast) -> i64 {
    let bonus: i64 = match mode {
        .Fast => 0,
        .Exact => 1,
    };
    return (r.w * sx) * (r.h * sy) + bonus;
}

fn combine<T>(x: T, y: T = 0, z: T = 0) -> T {
    return x * 100 + y * 10 + z;
}

fn pick(a: i64, b: i64) -> i64 { return a + b; }
fn pick(a: i64, c: i64) -> i64 { return a * 100 + c; }

fn main() {
    print(pack(1, c: 9), " ", pack(d: 7, a: 2), " ", pack(5, d: 8, b: 6));

    let rect = Rect { w: 3, h: 4 };
    print(rect.scale(sy: 2), " ", rect.scale(mode: .Exact, sx: 2, sy: 3));

    print(combine(1, z: 7), " ", combine(z: 9, x: 4, y: 5));
    print(pick(a: 2, b: 3), " ", pick(c: 5, a: 2));
}
