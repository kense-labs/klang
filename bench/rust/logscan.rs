use std::{env, fs, process, time::Instant};

#[derive(Default)]
struct Summary {
    rows: u64,
    total: u64,
    max: usize,
    classes: [u64; 5],
    p50: usize,
    p95: usize,
    p99: usize,
}

fn summarize(data: &[u8]) -> Result<Summary, u64> {
    let mut summary = Summary::default();
    let mut counts = [0_u64; 10000];
    let mut pos = 0;
    while pos < data.len() {
        let row = summary.rows + 1;
        if data.len() - pos < 5 {
            return Err(row);
        }
        let first = data[pos];
        if !(b'1'..=b'5').contains(&first)
            || !data[pos + 1].is_ascii_digit()
            || !data[pos + 2].is_ascii_digit()
            || data[pos + 3] != b','
        {
            return Err(row);
        }
        pos += 4;
        let start = pos;
        let mut micros = 0;
        while pos < data.len() && data[pos].is_ascii_digit() {
            if pos - start == 4 {
                return Err(row);
            }
            micros = micros * 10 + (data[pos] - b'0') as usize;
            pos += 1;
        }
        if pos == start {
            return Err(row);
        }
        if pos < data.len() {
            if data[pos] == b'\r' {
                pos += 1;
            }
            if pos == data.len() || data[pos] != b'\n' {
                return Err(row);
            }
            pos += 1;
        }
        summary.rows += 1;
        summary.total += micros as u64;
        if micros > summary.max {
            summary.max = micros;
        }
        summary.classes[(first - b'1') as usize] += 1;
        counts[micros] += 1;
    }
    if summary.rows > 0 {
        let rank50 = (summary.rows * 50).div_ceil(100);
        let rank95 = (summary.rows * 95).div_ceil(100);
        let rank99 = (summary.rows * 99).div_ceil(100);
        let mut seen = 0;
        for (value, count) in counts.iter().enumerate().take(summary.max + 1) {
            let next = seen + count;
            if seen < rank50 && next >= rank50 {
                summary.p50 = value;
            }
            if seen < rank95 && next >= rank95 {
                summary.p95 = value;
            }
            if next >= rank99 {
                summary.p99 = value;
                break;
            }
            seen = next;
        }
    }
    Ok(summary)
}

fn fail(message: &str) -> ! {
    eprintln!("{message}");
    process::exit(2);
}

fn main() {
    let args: Vec<_> = env::args_os().collect();
    let timed = args.len() == 3 && args[2] == "--timings";
    if args.len() != 2 && !timed {
        fail("usage: logscan FILE [--timings]");
    }
    let start = Instant::now();
    let data = fs::read(&args[1]).unwrap_or_else(|_| fail("cannot read input"));
    let read_done = Instant::now();
    let summary = summarize(&data).unwrap_or_else(|row| fail(&format!("invalid row {row}")));
    let compute_done = Instant::now();
    let avg = summary.total.checked_div(summary.rows).unwrap_or(0);
    let [s1, s2, s3, s4, s5] = summary.classes;
    println!(
        "rows={} status={s1},{s2},{s3},{s4},{s5} avg_us={avg} max_us={} p50_us={} p95_us={} p99_us={}",
        summary.rows, summary.max, summary.p50, summary.p95, summary.p99
    );
    if timed {
        eprintln!(
            "read_ns={} compute_ns={}",
            read_done.duration_since(start).as_nanos(),
            compute_done.duration_since(read_done).as_nanos()
        );
    }
}
