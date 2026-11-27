// error: another scoped task may alias the mutable array
async fn read(values: const i64[..4]&) -> i64 { return values[0]; }
async fn reset(values: i64[..4]&) { values.clear(); }

fn main() {
    var values: i64[..4] = [6, 9];
    task_scope(read(values), reset(values)) { scope =>
        print(scope.join(0));
    }
}
