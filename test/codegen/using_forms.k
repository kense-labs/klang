// C#-style `using` declarations and blocks:
// - `using let/var x = expr;` disposes at enclosing block exit (LIFO order)
// - `using (let/var x = expr) { ... }` disposes at inner block exit
// - `using (expr) { ... }` anonymous resource guard with AST dump roundtrip
// - early return, loop continue/break, value-returning blocks, struct literals
// - backward compatibility with `using` as an ordinary identifier/function name

struct Res { id: i64 }

fn open_res(id: i64) -> Res { Res { id: id } }
fn dispose(r: Res) { print("dispose res ", r.id); }

struct MutRes { tag: i64, dirties: i64 }

fn dispose(r: MutRes&) { print("dispose mut ", r.tag, " dirties=", r.dirties); }

struct Guard { label: const u8[:] }

fn dispose(g: Guard) { print("dispose guard ", g.label); }

fn declaration_lifo(early: bool) -> i64 {
    using let a = open_res(1);
    using let b = open_res(2);
    print("body a=", a.id, " b=", b.id);
    if early {
        return 10;
    }
    using let c = open_res(3);
    print("body c=", c.id);
    return 20;
}

fn mutable_and_blocks() -> i64 {
    using var m = MutRes { tag: 7, dirties: 0 };
    m.dirties++;
    using (let inner = open_res(4)) {
        print("inner ", inner.id);
        m.dirties++;
    }
    print("after inner");
    using (var mut_scoped = MutRes { tag: 5, dirties: 1 }) {
        mut_scoped.dirties += 2;
        print("scoped ", mut_scoped.tag);
    }
    let val = block {
        using (let t = open_res(6)) {
            print("block ", t.id);
            break 42;
        }
    };
    return val;
}

fn value_returning_using() -> i64 {
    using (let r = open_res(8)) {
        print("returning ", r.id);
        r.id * 10
    }
}

fn anonymous_and_loops() {
    using (Guard { label: "outer" }) {
        print("in anon outer");
        for i in 4 {
            using let iter_r = open_res(100 + i);
            if i == 1 { continue; }
            if i == 3 { break; }
            print("loop ", i);
        }
        using (Guard { label: "inner" }) {
            print("in anon inner");
        }
    }
    print("after anon");
}

fn using(x: i64) -> i64 { x + 1 }

fn compat_var() -> i64 {
    let using = 9;
    using
}

fn compat_identifier() {
    let val = compat_var();
    let bumped = using(val);
    print("compat ", val, " ", bumped);
}

fn main() {
    print("ret1=", declaration_lifo(true));
    print("ret2=", declaration_lifo(false));
    print("mut=", mutable_and_blocks());
    print("val=", value_returning_using());
    anonymous_and_loops();
    compat_identifier();
}
