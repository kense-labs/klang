// error: type Item has no field missing
decorator Column(id: u8) on field;
struct Item { @Column(1) field: i64 }

fn main() { print(decorator_metadata<Column, Item>(missing).id); }
