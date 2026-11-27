async fn read(values: const i64[>..<]&) -> i64 {
    let view = values[..];
    await yield_now();
    return view[0];
}

async fn append(values: i64[>..<]&) { values.push(13); }

fn main() {
    var values: i64[>..<] = [6, 9];
    task_scope(read(values)) { outer =>
        assert(outer.step());
        print("reader pending");
        task_scope(append(values)) { inner =>
            inner.join(0);
        }
    }
}
