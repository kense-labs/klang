// error: padding cannot carry a decorator
decorator Marker() on field;
struct Record { @Marker pad 4, value: i64 }

fn main() {}
