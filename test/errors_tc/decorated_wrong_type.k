// error: decorated requires a declared decorator type
struct Plain { value: i64 }

fn main() {
    const for <Name, Declaration, Metadata> in decorated<Plain, namespace ::> {
        print(Name);
    }
}
