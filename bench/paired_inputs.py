"""Generate frozen file workloads and their independent expected results."""
from collections import Counter
from hashlib import file_digest


def digest(path):
    with path.open("rb") as source:
        return file_digest(source, "sha256").hexdigest()


def logscan(path, rows):
    # An explicit 32-bit generator keeps fixture bytes independent of the
    # Python version. The oracle counts the values before printing the CSV.
    state = 0x317A925B
    classes = [0] * 5
    histogram = Counter()
    with path.open("wb") as output:
        for row in range(rows):
            state = (1664525 * state + 1013904223) & 0xFFFFFFFF
            sample = (state >> 16) % 100
            status = 200 if sample < 82 else 302 if sample < 89 else 404 if sample < 97 else 503
            state = (1664525 * state + 1013904223) & 0xFFFFFFFF
            latency = (state >> 8) % (10000 if row % 10 == 0 else 1600)
            classes[status // 100 - 1] += 1
            histogram[latency] += 1
            output.write(f"{status},{latency}\n".encode("ascii"))

    def percentile(p):
        rank = (rows * p + 99) // 100
        for value, count in sorted(histogram.items()):
            rank -= count
            if rank <= 0:
                return value
        return 0

    total = sum(value * count for value, count in histogram.items())
    average = total // rows if rows else 0
    return (f"rows={rows} status={','.join(map(str, classes))} avg_us={average} "
            f"max_us={max(histogram, default=0)} p50_us={percentile(50)} "
            f"p95_us={percentile(95)} p99_us={percentile(99)}")


def file_copy(path, size):
    chunk = bytes(range(256)) * 256
    count, tail = divmod(size, len(chunk))
    with path.open("wb") as output:
        for _ in range(count):
            output.write(chunk)
        output.write(chunk[:tail])
    return str(size)
