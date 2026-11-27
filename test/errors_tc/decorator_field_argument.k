// error: decorator argument requires a scalar or string constant
decorator Column(id: u8) on field;

fn number() -> u8 { 1 }
enum Event {
    Data { @Column(number()) field: i64 },
}

fn main() {}
