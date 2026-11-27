import std;
import os;

struct Summary {
    rows: i64,
    total: i64,
    max: i64,
    classes: i64[5],
    p50: i64,
    p95: i64,
    p99: i64,
}

fn digit(b: u8) -> bool { b >= '0' && b <= '9' }

fn summarize(data: const u8[:]) -> Summary, i64 {
    var summary = Summary { rows: 0, total: 0, max: 0, classes: [0; 5], p50: 0, p95: 0, p99: 0 };
    var counts: i64[10000] = [0; 10000];
    var pos = 0;
    while pos < data.len {
        let row = summary.rows + 1;
        if data.len - pos < 5 { return summary, row; }
        let first = data[pos];
        if first < '1' || first > '5' || !digit(data[pos + 1]) ||
            !digit(data[pos + 2]) || data[pos + 3] != ',' { return summary, row; }
        pos += 4;
        let start = pos;
        var micros = 0;
        while pos < data.len && digit(data[pos]) {
            if pos - start == 4 { return summary, row; }
            micros = micros * 10 + (data[pos] - '0');
            pos++;
        }
        if pos == start { return summary, row; }
        if pos < data.len {
            if data[pos] == '\r' { pos++; }
            if pos == data.len || data[pos] != '\n' { return summary, row; }
            pos++;
        }
        summary.rows++;
        summary.total += micros;
        if micros > summary.max { summary.max = micros; }
        summary.classes[(first - '1') as i64]++;
        counts[micros]++;
    }
    if summary.rows > 0 {
        let rank50 = (summary.rows * 50 + 99) / 100;
        let rank95 = (summary.rows * 95 + 99) / 100;
        let rank99 = (summary.rows * 99 + 99) / 100;
        var seen = 0;
        for value in summary.max + 1 {
            let next = seen + counts[value];
            if seen < rank50 && next >= rank50 { summary.p50 = value; }
            if seen < rank95 && next >= rank95 { summary.p95 = value; }
            if next >= rank99 { summary.p99 = value; break; }
            seen = next;
        }
    }
    return summary, 0;
}

fn fail(message: const u8[:]) {
    write_stderr(message);
    write_stderr("\n");
    exit(2);
}

fn main() {
    let argv = args();
    let timed = argv.len == 3 && argv[2] == "--timings";
    if argv.len != 2 && !timed { fail("usage: logscan FILE [--timings]"); }
    var data: u8[>..] = [];
    let start = clock_ns();
    if !read_file(argv[1], data) { fail("cannot read input"); }
    let read_done = clock_ns();
    let summary, bad_row = summarize(data);
    let compute_done = clock_ns();
    if bad_row != 0 { fail(str("invalid row ", bad_row)); }
    let avg = if summary.rows == 0 { 0 } else { summary.total / summary.rows };
    print("rows=", summary.rows,
          " status=", summary.classes[0], ",", summary.classes[1], ",", summary.classes[2], ",",
          summary.classes[3], ",", summary.classes[4],
          " avg_us=", avg, " max_us=", summary.max,
          " p50_us=", summary.p50, " p95_us=", summary.p95, " p99_us=", summary.p99);
    if timed {
        write_stderr(str("read_ns=", read_done - start, " compute_ns=", compute_done - read_done, "\n"));
    }
}
