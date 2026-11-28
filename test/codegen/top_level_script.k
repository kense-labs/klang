struct Point {
    x: i64,
    y: i64,
}

struct ScopeGuard { label: const u8[:] }
fn dispose(g: ScopeGuard) { print("dispose=", g.label); }

fn helper_before(n: i64) -> i64 { n * 2 }

let global_scale = 3;

using let g1 = ScopeGuard { label: "top" };
defer print("top_defer");

var sum = 0;
for i in 1..4 {
    sum += i;
}

let { x, y } = Point { x: 10, y: 20 };
guard sum == 6 else {
    print("bad sum");
    return;
}

print("sum=", sum, " helper=", helper_before(5), " scale=", global_scale, " after=", helper_after(x + y));

fn helper_after(v: i64) -> i64 { v + 1 }
