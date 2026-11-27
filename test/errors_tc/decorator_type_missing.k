// error: declaration has no @Entity decorator
decorator Entity() on struct;
struct Item { field: i64 }

fn main() { assert(decorator_metadata<Entity, Item>() == Entity {}); }
