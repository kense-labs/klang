struct Node {
    val:  i64,
    next: Node? = null,
}

fn bumped(n: Node&, delta: i64 = 1) -> i64 {
    return n.val + delta;
}

fn child_of(n: Node&) -> Node? {
    return n.next;
}

fn record_visit(n: Node&, hits: i64&) {
    hits += n.val;
}

fn fallback_eval(calls: i64&, v: i64) -> i64 {
    calls++;
    return v;
}

fn main() {
    let n3 = Node { val: 30 };
    let n2 = Node { val: 20, next: n3 };
    let n1 = Node { val: 10, next: n2 };
    let opt1:  Node? = n1;
    let empty: Node? = null;

    let opt2:     Node? = opt1?.next;
    let opt_none: Node? = empty?.next;
    let v_field:  i64   = opt1?.next?.val ?? -1;
    let v_deep:   i64   = opt1?.next?.next?.val ?? -1;
    let v_chain:  i64   = opt_none?.val ?? empty?.next?.val ?? opt2?.val ?? 99;
    print(v_field, " ", v_deep, " ", v_chain);

    let m_hit:      i64 = opt1?.next?.bumped(5) ?? 0;
    let m_miss:     i64 = empty?.bumped(delta: 9) ?? 77;
    let m_opt:      i64 = opt1?.child_of()?.val ?? -9;
    let m_opt_miss: i64 = n3.next?.child_of()?.val ?? -9;
    print(m_hit, " ", m_miss, " ", m_opt, " ", m_opt_miss);

    var hits: i64 = 0;
    opt1?.record_visit(hits);
    empty?.record_visit(hits);
    print("hits=", hits);

    var rhs_calls: i64 = 0;
    let short_a: i64 = opt1?.val ?? fallback_eval(rhs_calls, 500);
    let short_b: i64 = empty?.val ?? fallback_eval(rhs_calls, 500);
    print(short_a, " ", short_b, " rhs_calls=", rhs_calls);
}
