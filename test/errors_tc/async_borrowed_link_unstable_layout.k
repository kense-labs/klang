// error: borrowed task input input needs a stable layout within one backing owner
struct Buffer { items: i64[..3] }
struct Access { target: Buffer&<u32>? }
struct Row { data: Buffer, access: Access }

async fn clear(input: Access&) {
    let target .= input.target;
    await yield_now();
    if target { target.items.clear(); }
}

async fn owner() -> i64 {
    var rows: Row[..3] = [Row { Buffer { [41] }, Access { null } }];
    rows.push(Row { Buffer { [] }, Access { rows[0].data } });
    let view = rows[0].data.items[..];
    await clear(rows[1].access);
    return view[0];
}

fn main() { print(owner().join()); }
