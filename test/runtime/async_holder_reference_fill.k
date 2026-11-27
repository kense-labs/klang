struct Slot { value: const i64? }

async fn read(slots: const Slot[2]&) -> bool {
    await yield_now();
    return !slots[0].value;
}

fn reset(slots: Slot[:]) {
    for i in slots.len { slots[i] = default<Slot>(); }
}

fn main() {
    let value: i64 = 3;
    var slots: Slot[2] = [Slot { value }, Slot { null }];
    task_scope(read(slots)) { scope =>
        assert(scope.step());
        print("reader pending");
        reset(slots[..]);
        print(scope.join(0));
    }
}
