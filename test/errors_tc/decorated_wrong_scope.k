// error: a field decorator needs a struct or enum variant scope
decorator Label(id: i64) on field;

fn main() {
    const for <Name, Field, Metadata> in decorated<Label, namespace ::> {
        print(Name, " ", Metadata.id);
    }
}
