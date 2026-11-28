struct ServerConfig {
    host:    const u8[:],
    port:    i64,
    tls:     bool,
    workers: i64 = 4,
}

struct Pair<T> {
    first:  T,
    second: T,
}

struct Envelope {
    tag:  const u8[:],
    vals: i64[],
}

fn main() {
    let base = ServerConfig { host: "example.com", port: 80, tls: false };
    let prod = ServerConfig { port: 443, tls: true, ..base };
    let { host, port, tls, workers } = prod;
    print(host, ":", port, " tls=", tls, " workers=", workers);

    let { host: h, port: p } .= base;
    print(h, ":", p);

    let gp = Pair<i64> { first: 10, second: 20 };
    let gp2 = Pair<i64> { second: 99, ..gp };
    var { first: a, second: b } = gp2;
    a += 5;
    print(a, " ", b);

    const k_pair = Pair<i64> { first: 3, second: 4 };
    const { first: ca, second: cb } = k_pair;
    print(ca + cb);

    let env1 = Envelope { tag: "v1", vals: [1, 2, 3] };
    let env2 = Envelope { vals: [4, 5], ..env1 };
    let { tag, vals } = env2;
    print(tag, " len=", vals.len, " sum=", vals[0] + vals[1]);
}
