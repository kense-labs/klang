use std::{env, fs, process};

// Read a regular binary file into an owned byte buffer, then write it.
fn main() {
    let args: Vec<_> = env::args_os().collect();
    if args.len() != 3 {
        process::exit(2);
    }
    let data = fs::read(&args[1]).unwrap_or_else(|_| process::exit(2));
    fs::write(&args[2], &data).unwrap_or_else(|_| process::exit(2));
    println!("{}", data.len());
}
