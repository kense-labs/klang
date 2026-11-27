struct View { data: const i64[:], tag: i64 }

var numbers: i64[3] = [10, 20, 30];
var shared: View[..6] = [View { numbers[..], 1 }, View { numbers[1..], 2 }];

async fn read(input: const View&, turns: i64) -> i64 {
    for i in turns { await yield_now(); }
    return input.data[0] + input.tag;
}

async fn mutate_other(input: const View&, padding: i64[6]) -> i64 {
    await yield_now();
    shared[1] = View { numbers[2..], padding[5] };
    return input.data[0] + input.tag;
}

async fn child() -> i64 {
    return await mutate_other(shared[0], [1, 2, 3, 4, 5, 6]);
}

fn scoped() {
    var cells: View[..8] = [View { numbers[..], 1 }, View { numbers[1..], 2 },
                              View { numbers[2..], 3 }];
    var index: i64 = 0;
    fn choose() -> i64 { index += 1; return index - 1; }
    task_scope(read(cells[choose()], 1), read(cells[choose()], 4)) { scope =>
        assert(index == 2);
        assert(scope.step());
        cells[2] = View { numbers[..], 7 };
        cells.push(View { numbers[..], 8 });
        cells.append([View { numbers[..], 9 }]);
        cells.resize(6, View { numbers[..], 10 });
        print("first ", scope.join(0));
        assert(!scope.finished(1));
        cells[0] = View { numbers[2..], 11 };
        print("second ", scope.join(1));
    }
    assert(cells[0].data[0] == 30 && cells[2].tag == 7 && cells[5].tag == 10);

    var dynamic: View[..] = [..3];
    dynamic.push(View { numbers[1..], 5 });
    task_scope(read(dynamic[0], 2)) { scope =>
        assert(scope.step());
        dynamic.push(View { numbers[..], 3 });
        scope.cancel(0);
        assert(dynamic.len == 2);
    }
}

fn conditional(use_left: bool) -> i64 {
    var left: View[..4] = [View { numbers[..], 1 }];
    var right: View[..4] = [View { numbers[1..], 2 }];
    let a: const View& = left[0];
    let b: const View& = right[0];
    let selected: const View& = if use_left { a } else { b };
    return task_scope(read(selected, 2)) { scope =>
        assert(scope.step());
        left.push(View { numbers[2..], 3 });
        right.push(View { numbers[..], 4 });
        scope.join(0)
    };
}

fn main() {
    scoped();
    print("joined ", mutate_other(shared[0], [1, 2, 3, 4, 5, 7]).join(), " ", shared[1].tag);
    print("awaited ", child().join(), " ", shared[1].tag);
    print("conditional ", conditional(true), " ", conditional(false));
}
