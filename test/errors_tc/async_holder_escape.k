// error: a task with borrowed arguments needs direct await, join, or task_scope
struct View { rows: const i64[:] }

async fn sum(input: View) -> i64 { return input.rows[0]; }

fn main() {
    let rows: i64[2] = [1, 2];
    let task = sum(View { rows[..] });
    print(task.join());
}
