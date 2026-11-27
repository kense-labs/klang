import .decorator_support.layouts;
namespace schema = schemas;

type Packet32 = schema::Packet<u32>;
type ColumnId = schema::Column;
type Incoming = schema::Event<u32>.Received;

struct PlainPacket {
    let id: u16 = 12,
    pad,
    value: u32,
}

enum PlainEvent {
    Received { value: u32 },
    Sent { value: u32 },
    Empty,
}

let packet_schema = decorator_metadata<schema::Schema, schema::Packet>();

fn column<T>() -> ColumnId {
    return decorator_metadata<ColumnId, T>(value);
}

fn main() {
    let id = 900;  // The field selector never reads the local.
    let column_id = decorator_metadata<ColumnId, Packet32>(id);
    let wide_column = column<schema::Packet<i64>>();
    let incoming = decorator_metadata<ColumnId, Incoming>(value);
    let outgoing = decorator_metadata<ColumnId, schema::Event.Sent>(value);
    let kind = decorator_metadata<schema::Entry, schema::Event<i64>>();
    assert(decorator_metadata<schema::Schema, Packet32>() == packet_schema);
    assert(decorator_metadata<schema::Public, Packet32>(id) == schema::Public {});
    print(packet_schema.name, " ", packet_schema.version, " ", id);
    print(column_id.number, " ", wide_column.number, " ", kind.kind, " ", incoming.number, " ", outgoing.number);

    let packet = Packet32 { value: 83 };
    let plain = PlainPacket { value: 83 };
    let packets = [packet];
    let plain_packets = [plain];
    let packet_bytes = bytes_of(packets);
    let plain_bytes = bytes_of(plain_packets);
    assert(packet_bytes.len == plain_bytes.len && packet_bytes.len == 8);
    assert(packet_bytes[0..2] == plain_bytes[0..2]);
    assert(packet_bytes[4..8] == plain_bytes[4..8]);
    let event: schema::Event<u32> = Incoming { value: 42 };
    let plain_event: PlainEvent = PlainEvent.Received { value: 42 };
    assert(bytes_of([event]) == bytes_of([plain_event]));
    var buckets = schema::Buckets { values: [41] };
    buckets.values.push(42);
    let root_schema = decorator_metadata<schema::Schema, schema::Buckets>();
    let root_column = decorator_metadata<ColumnId, schema::Buckets>(values);
    print(root_schema.name, " ", root_column.number, " ", buckets.values[1]);
}
