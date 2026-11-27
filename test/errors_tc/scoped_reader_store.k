// error: function values are compile-time only and cannot be stored
import os;

fn main() {
    with_read_file("unused") { read =>
        let escaped = read;
    };
}
