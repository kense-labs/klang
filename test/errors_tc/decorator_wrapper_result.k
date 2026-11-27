// error: expected a value of type i64, got bool
decorator Wrong() on fn { wrap { return false; } }
@Wrong
fn inferred<T>(value: T) { return value; }
fn main() { print(inferred(8)); }
