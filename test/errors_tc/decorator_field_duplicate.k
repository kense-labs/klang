// error: decorator Column is applied more than once
decorator Column(id: u8) on field;
type Alias = Column;

struct Record {
    @Column(1) @Alias(2) field: i64,
}

fn main() {}
