import std;
import os;

fn byte_after_two(data: const u8[:], pos: i64) -> i64 {
    if pos < 0 || data.len - pos < 3 { return -1; }
    return data[pos + 2]; // bce:elide
}

fn byte_in_prefix(data: const u8[:], pos: i64) -> i64 {
    if pos >= 0 && 2 < data.len - pos {
        return data[pos + 1]; // bce:elide
    }
    return -1;
}

fn unchecked_subtraction(data: const u8[:], pos: i64) -> i64 {
    if data.len - pos < 3 { return -1; }
    return data[pos + 2]; // bce:keep
}

fn main() {
    print(byte_after_two("abcd", 1), " ", byte_after_two("abcd", 2), " ",
          byte_after_two("abcd", -1)); // 100 -1 -1
    print(byte_in_prefix("abcd", 1), " ", byte_in_prefix("abcd", 2)); // 99 -1
    let pos = if args().len > 0 { 0 } else { -3 };
    print(unchecked_subtraction("abcd", pos)); // 99
}
