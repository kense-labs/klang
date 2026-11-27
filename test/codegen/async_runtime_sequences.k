import std;
import os;

async fn grow(seed: i64) -> i64 {
    var numbers: i64[>..] = [seed, seed + 1];
    let audit: i64[] = [seed * 10];
    var sum = 0;
    defer { print("cleanup ", seed, " ", numbers.len, " ", audit[0], " ", sum); }
    for number, index in numbers {
        if index == 0 { numbers.push(seed + 2); }
        sum += await yield_now(number);
    }
    return sum;
}

async fn linear_parent(seed: i64) -> i64 {
    let result: i64 = await grow(seed);
    return result;
}

async fn structured_parent(seed: i64) -> i64 {
    return 1 + await grow(seed);
}

struct Buffer { tag: i64, elements: i64[>..] }

async fn formats() -> i64 {
    let packed: u8[varint] = [2, 3, 5];
    var static_limit: i64[..4] = [7, 11];
    var runtime_limit: i64[..] = [..4];
    runtime_limit.push(13);
    var buffer = Buffer { 17, [19, 23] };
    var result = 0;
    for number in packed {
        await yield_now();
        result += number;
    }
    for number, i in static_limit {
        await yield_now();
        result += number;
        if i == 0 { static_limit.push(29); }
    }
    for number, i in runtime_limit {
        await yield_now();
        result += number;
        if i == 0 { runtime_limit.push(31); }
    }
    for &number in buffer.elements {
        number += buffer.tag;
        result += number;
        await yield_now();
    }
    return result;
}

fn boundary(n: i64) -> i64 {
    print("bound ", n);
    return n;
}

async fn range_view() -> i64 {
    var data: i64[>..] = [1, 2, 3, 4, 5];
    var sum = 0;
    var first = 1;
    var end = 4;
    for number, index in data[boundary(first)..boundary(end)] {
        await yield_now();
        sum += number;
        if index == 0 {
            first = 0;
            end = 5;
            data.push(50);
        }
    }
    return sum + first + end + data.len;
}

fn main() {
    task_scope(grow(10), grow(100)) { scope =>
        assert(scope.step());
        assert(scope.step());
        assert(trim_memory());
        scope.cancel(0);
        print("joined ", scope.join(1));
    }
    print("linear ", linear_parent(3).join());
    print("structured ", structured_parent(5).join());
    print("formats ", formats().join());
    print("slice ", range_view().join());
}
