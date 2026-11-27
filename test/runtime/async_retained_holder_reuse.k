struct View { data: const i64[:] }

var source: i64[2] = [11, 21];
var views: View[..3] = [View { source[..] }];

async fn read() {
    let saved: const View& = views[0];
    views.clear();
    defer { print(saved.data[0]); }
    await yield_now();
}

fn main() {
    task_scope(read()) { scope =>
        assert(scope.step());
        print("cancel cleanup still needs the popped slot");
        views.push(View { source[1..] });
        scope.cancel(0);
    }
}
