import os;

export "c_check_upper" fn check_upper(data: const u8[:], pos: i64, count: u8) -> i64 {
    if pos < 0 || pos >= data.len { return -1; }
    return data[pos..pos + count].len; // bce:keep
}

export "c_check_lower" fn check_lower(data: const u8[:], lo: i64) -> i64 {
    if lo > 0 { return -1; }
    return data[lo..0].len; // bce:keep
}

export "c_check_order" fn check_order(data: const u8[:], lo: i64, hi: i64) -> i64 {
    if lo < 0 || hi < 0 || hi > data.len { return -1; }
    return data[lo..hi].len; // bce:keep
}

fn main() {
    let argv = args();
    if argv.len > 1 {
        if argv[1] == "upper" { print(check_upper("abcd", 2, 3)); }
        else if argv[1] == "lower" { print(check_lower("abcd", -1)); }
        else { print(check_order("abcd", 3, 2)); }
        return;
    }
    print(check_upper("abcd", 1, 2), " ", check_lower("abcd", 0), " ",
          check_order("abcd", 1, 3));
}
