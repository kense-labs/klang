// error: decorator Entity requires struct
decorator Entity() on struct;

@Entity
enum Event { Ready }

fn main() {}
