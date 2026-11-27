namespace registry;

@Handler(8)
fn dispatch(value: i64) -> i64 { return value + 8; }

const PeerWeight: i64 = 16;

@registry::Handler(PeerWeight)
fn peer::dispatch(value: i64) -> i64 { return value + 16; }
