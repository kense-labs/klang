// error: a field metadata lookup needs a struct or enum variant type
decorator Column(id: u8) on field;
enum Item { Some { @Column(1) field: i64 } }

fn main() { print(decorator_metadata<Column, Item>(field).id); }
