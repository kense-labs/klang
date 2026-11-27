// error: no matching overload
decorator ByteOnly() on fn;

@ByteOnly
fn accept(value: u8) { print(value); }
fn accept(value: i64) { print(value); }

fn main() {
    const for <Name, Handler, Metadata> in decorated<ByteOnly, namespace ::> {
        Handler(-1);
    }
}
