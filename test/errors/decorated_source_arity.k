// error: const for source requires exactly 3 binding(s)
decorator Marker() on fn;

fn main() {
    const for <Name, Handler> in decorated<Marker, namespace ::> {}
}
