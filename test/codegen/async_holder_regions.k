struct View { data: const i64[:], tag: i64 }
struct Shelf { front: View, tail: View[>..] }
struct Link { view: const View& }

var source: i64[..4] = [10, 20, 30];
var shelf = Shelf { View { source[0..1], 1 }, [View { source[1..2], 2 }] };
var link = Link { shelf.tail[0] };

async fn read(input: const View&) -> i64 {
    defer { print("cleanup ", input.data[0], " ", input.tag); }
    await yield_now();
    return input.data[0] + input.tag;
}

async fn follow(input: const Link&) -> i64 { return await read(input.view); }

async fn local() -> i64 {
    var data: i64[>..] = [40, 50];
    var views: View[>..<] = [View { data[..], 4 }];
    let focus: const View& = views[0];
    await yield_now();
    views.push(View { data[1..], 5 });
    views[0].tag += 1;
    return await read(focus);
}

fn main() {
    task_scope(read(shelf.front), follow(link)) { scope =>
        assert(scope.step());
        assert(scope.step());
        shelf.tail.push(View { source[2..], 3 });
        shelf.front.tag = 5;
        print("front ", scope.join(0));
        shelf.tail[0].tag = 6;
        print("tail ", scope.join(1));
    }
    var reserved: View[..] = [..4];
    reserved.push(View { source[2..], 7 });
    task_scope(read(reserved[0])) { scope =>
        assert(scope.step());
        scope.cancel(0);
        reserved[0] = View { source[..], 8 };
        reserved.push(View { source[..], 9 });
    }
    var packed: View[] = [View { source[..], 10 }];
    print("packed ", read(packed[0]).join());
    print("local ", local().join());
}
