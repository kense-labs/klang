struct View { data: const i64[:], tag: i64 }

var source: i64[3] = [10, 20, 30];
var views: View[..6] = [View { source[..], 1 }, View { source[1..], 2 },
                       View { source[2..], 3 }];
var alternate: View[..4] = [View { source[1..], 4 }];

async fn child(input: const View&, tag: i64) -> i64 {
    await yield_now();
    views[1] = View { source[..], tag };
    return input.data[0] + input.tag;
}

async fn parent() -> i64 {
    var saved: const View& = views[1];
    let result = await child(views[0], { saved .= views[2]; 7 });
    return result + saved.data[0] + saved.tag;
}

async fn retained(use_left: bool) -> i64 {
    let a: const View& = views[0];
    let b: const View& = alternate[0];
    let selected: const View& = if use_left { a } else { b };
    defer { print("cleanup ", selected.data[0], " ", selected.tag); }
    await yield_now();
    return selected.data[0] + selected.tag;
}

async fn local_slot() -> i64 {
    var dynamic: View[..] = [..3];
    dynamic.push(View { source[1..], 5 });
    let saved: const View& = dynamic[0];
    await yield_now();
    dynamic.push(View { source[..], 6 });
    return saved.data[0] + saved.tag + dynamic.len;
}

fn main() {
    print("awaited ", parent().join(), " ", views[1].tag);
    task_scope(retained(true)) { scope =>
        assert(scope.step());
        views[2] = View { source[..], 8 };
        views.append([View { source[..], 9 }]);
        alternate[0] = View { source[2..], 6 };
        scope.cancel(0);
        views[0] = View { source[1..], 2 };
    }
    task_scope(retained(false)) { scope =>
        assert(scope.step());
        views[0] = View { source[..], 1 };
        print("joined ", scope.join(0));
        alternate[0] = View { source[..], 10 };
    }
    print("local ", local_slot().join());
}
