// error: return cannot leave a defer body
fn f() -> i64 {
    errdefer {
        return 1;
    }
    return 0;
}
fn main() { f(); }
