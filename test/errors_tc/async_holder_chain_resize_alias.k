// error: another scoped task may alias the mutable array
struct Data { value: const i64[:] }
struct Entry { data: const Data& }

async fn read(input: const Entry&) -> i64 { return input.data.value[0]; }
async fn clear(values: i64[..4]&) { values.clear(); }

fn main() {
    var values: i64[..4] = [21];
    let data = Data { values[..] };
    let entry = Entry { data };
    task_scope(read(entry), clear(values)) { scope => print(scope.join(0)); }
}
