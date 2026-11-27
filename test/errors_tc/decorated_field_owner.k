// error: const for accessor belongs to A, not B
decorator Label() on field;
struct A { @Label value: i64 }
struct B { value: i64 }

fn main() {
    let other = B { 9 };
    const for <Name, Field, Metadata> in decorated<Label, A> {
        print(other.Field);
    }
}
