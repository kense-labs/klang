enum Color { Red, Green, Blue }

enum Shape {
    Circle { r: i64 },
    Rect { w: i64, h: i64 },
    Empty,
}

struct Palette {
    primary: Color,
    accent:  Color = .Blue,
}

fn color_code(c: Color) -> i64 {
    return match c {
        .Red => 1,
        .Green => 2,
        .Blue => 3,
    };
}

fn pick_color(flag: bool) -> Color {
    if flag {
        return .Red;
    }
    return .Blue;
}

fn area(s: Shape) -> i64 {
    return match s {
        .Circle c => c.r * c.r,
        .Rect r => r.w * r.h,
        .Empty => 0,
    };
}

fn main() {
    var c: Color = .Green;
    print(color_code(c));
    c = .Red;
    print(color_code(c), " ", color_code(.Blue), " ", color_code(pick_color(true)));

    let pal = Palette { primary: .Green };
    let eq: i64 = if pal.primary == .Green { 1 } else { 0 };
    let ne: i64 = if pal.accent != .Red { 1 } else { 0 };
    print(eq, " ", ne, " ", color_code(pal.accent));

    let items: Color[] = [.Red, .Green, .Blue];
    print(color_code(items[0]), " ", color_code(items[1]), " ", color_code(items[2]));

    let s1: Shape = .Circle { r: 6 };
    var s2: Shape = .Empty;
    s2 = .Rect { w: 4, h: 5 };
    print(area(s1), " ", area(s2), " ", area(.Rect { w: 2, h: 9 }), " ", area(.Empty));
}
