decorator Schema() on struct;
decorator Column(id: u8) on field;

@Schema
struct Row {
    /// Its assigned column.
    @Column(1)
    let value: i64,
}

fn main() {
    let metadata = decorator_metadata<Column, Row>(value);
    print(metadata.id);
}

// lsp: hover 12:52 "let value: i64"
// lsp: hover 12:52 "Its assigned column."
// lsp: hover 12:39 "decorator Column on field"
// lsp: definition 12:52 -> 8:9
// lsp: definition 12:39 -> 2:11
// lsp: definition 12:47 -> 5:8
// lsp: token 12:52 property+readonly
