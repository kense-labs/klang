// error: function values cannot be returned
import os;

fn escape() {
    with_read_file("unused") { read =>
        return read;
    };
}

fn main() { escape(); }
