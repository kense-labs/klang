import std;
import os;

// Read a regular binary file into an owned byte buffer, then write it.
fn main() {
    let argv = args();
    if argv.len != 3 { exit(2); }
    var data: u8[>..] = [];
    if !read_file(argv[1], data) || !write_file(argv[2], data[..]) { exit(2); }
    print(data.len);
}
