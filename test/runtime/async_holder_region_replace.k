struct View { data: const i64[:] }
struct Shelf { prefix: View, tail: View[>..] }

async fn read(input: const View&) -> i64 {
    await yield_now();
    return input.data[0];
}

fn replace(input: Shelf&, data: const i64[:]) { input.tail[0] = View { data }; }

fn main() {
    let source: i64[2] = [11, 21];
    var shelf = Shelf { View { source[..] }, [View { source[..] }] };
    task_scope(read(shelf.tail[0])) { scope =>
        assert(scope.step());
        print("reader pending");
        replace(shelf, source[1..]);
        print(scope.join(0));
    }
}
