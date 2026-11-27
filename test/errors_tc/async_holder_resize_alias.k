// error: another scoped task may alias the mutable array
struct View { rows: const i64[:] }

async fn read(input: View) -> i64 { return input.rows[0]; }
async fn clear(values: i64[..4]&) { values.clear(); }

fn main() {
    var values: i64[..4] = [6, 9];
    task_scope(read(View { values[..] }), clear(values)) { scope =>
        print(scope.join(0));
    }
}
