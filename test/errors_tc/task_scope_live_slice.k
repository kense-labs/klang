// error: cannot call task scope, which shrinks values while view
// error: is still used: it may refer into values
var values: i64[>..<] = [1, 2, 3];

async fn shrink() { values.pop(); }

fn main() {
    task_scope(shrink()) { scope =>
        let view = values[0..2];
        scope.step();
        print(view[0]);
    }
}
