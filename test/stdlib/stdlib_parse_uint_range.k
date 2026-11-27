// runtime-debug: parse_uint reads every u64, including those above i64.max, and refuses what is outside u64 instead of
// wrapping; valid input must not trip K_DEBUG's overflow check inside std.
import std;

fn show(s: u8[:], base: i64 = 10) {
    let v, ok = parse_uint(s, base);
    print(v, " ", ok);
}

fn all_digits() -> bool {
    for byte in 256 {
        let d = if byte >= '0' && byte <= '9' { byte - '0' }
                else if byte >= 'A' && byte <= 'Z' { byte - 'A' + 10 }
                else if byte >= 'a' && byte <= 'z' { byte - 'a' + 10 }
                else { -1 };
        for base in -1..40 {
            let expected = if d < base { d } else { -1 };
            if parse_digit(byte as u8, base) != expected { return false; }
        }
    }
    return true;
}

fn bits_match(s: u8[:], signed: bool, want: u64, valid: bool, base: i64 = 10) -> bool {
    let bits, ok = parse_int_bits(s, signed, base);
    return bits == want && ok == valid;
}

fn main() {
    show("18446744073709551615");                                                   // 18446744073709551615 true
    show("+18446744073709551615");                                                  // 18446744073709551615 true
    show("18446744073709551616");                                                   // 0 false
    show("18446744073709551620");                                                   // 0 false
    show("184467440737095516150");                                                  // 0 false
    show("99999999999999999999");                                                   // 0 false
    show("9223372036854775807");                                                    // 9223372036854775807 true
    show("9223372036854775808");                                                    // 9223372036854775808 true
    show("12345678901234567890");                                                   // 12345678901234567890 true
    show("0000000000000000000018446744073709551615");                               // 18446744073709551615 true
    show("1111111111111111111111111111111111111111111111111111111111111111", 2);    // 18446744073709551615 true
    show("10000000000000000000000000000000000000000000000000000000000000000", 2);   // 0 false
    show("1777777777777777777777", 8);                                              // 18446744073709551615 true
    show("2000000000000000000000", 8);                                              // 0 false
    show("ffffffffffffffff", 16);                                                   // 18446744073709551615 true
    show("FFFFFFFFFFFFFFFF", 16);                                                   // 18446744073709551615 true
    show("10000000000000000", 16);                                                  // 0 false
    show("8000000000000000", 16);                                                   // 9223372036854775808 true
    show("3w5e11264sgsf", 36);                                                      // 18446744073709551615 true
    show("3W5E11264SGSG", 36);                                                      // 0 false
    show("Zz", 36);                                                                 // 1295 true
    show("1", 2);                                                                   // 1 true
    show("2", 2);                                                                   // 0 false
    show("7", 8);                                                                   // 7 true
    show("8", 8);                                                                   // 0 false
    show("9");                                                                      // 9 true
    show("a");                                                                      // 0 false
    show("f", 16);                                                                  // 15 true
    show("g", 16);                                                                  // 0 false
    show("0", 0);                                                                   // 0 false
    show("1", -16);                                                                 // 0 false
    show("0");                                                                      // 0 true
    show("+0");                                                                     // 0 true
    show("-0");                                                                     // 0 false
    show("-1");                                                                     // 0 false
    show("");                                                                       // 0 false
    show("+");                                                                      // 0 false
    show("++1");                                                                    // 0 false
    show(" 1");                                                                     // 0 false
    show("1 ");                                                                     // 0 false
    show("1_000");                                                                  // 0 false
    show("0x10");                                                                   // 0 false
    show("12x");                                                                    // 0 false
    show("10", 1);                                                                  // 0 false
    show("10", -1);                                                                 // 0 false
    show("10", 37);                                                                 // 0 false
    show("10", 64);                                                                 // 0 false
    show("ff", 36);                                                                 // 555 true
    show("ff", 37);                                                                 // 0 false
    print(all_digits());                                                            // true
    print(bits_match("-000000000000000000009223372036854775808", true, 9223372036854775808, true),
          " ", bits_match("+00000000000000000000", false, 0, true),
          " ", bits_match("-0", false, 0, false),
          " ", bits_match("0000000000000000000018446744073709551616", false, 0, false),
          " ", bits_match("00000000000000000000x", true, 0, false),
          " ", bits_match("+00000000000000000000Z", false, 35, true, 36));
}
