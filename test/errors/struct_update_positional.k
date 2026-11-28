// error: struct update (..base) requires named field initializers
struct P { x: i64, y: i64 }
fn main() {
    let base = P { x: 1, y: 2 };
    let _p = P { 3, ..base };
}
