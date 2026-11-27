namespace schemas;

decorator Schema(name: const u8[:], version: u8) on struct;
decorator Entry(kind: const u8[:]) on enum;
decorator Column(number: u8) on field;
decorator Public() on field;

const First: u8 = 1;

@Schema("packet", 2 + 1)
struct Packet<T> {
    @Column(First) @Public let id: u16 = 12,
    pad,
    @Column(First + 1) value: T,
}

@Entry("event")
enum Event<T> {
    Received { @Column(3) value: T },
    Sent { @Column(4) value: T },
    Empty,
}

@Schema("root", 1)
root struct Buckets {
    @Column(5) values: i64[>..<],
}
