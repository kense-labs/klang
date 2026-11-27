// dump-runtime
import .decorator_support.registry_z;
import .decorator_support.registry_a;
import .decorator_support.layouts;
namespace routes = registry;

type Handler = routes::Handler;
type Packet = schemas::Packet<u32>;

@Handler(31)
fn dispatch(value: i64) -> i64 { return value + 31; }

struct Reordered {
    @schemas::Column(8) later: u8,
    @schemas::Column(9) earlier: u8,
}

fn invoke<F>() -> i64 { return F(10); }

fn lookup(id: i64) -> i64 {
    match id {
        const for <Name, Field, Metadata> in decorated<schemas::Public, Packet> {
            4 => 24,
        },
        const for <Name, Field, Metadata> in decorated<schemas::Public, schemas::Event<u32>.Received> {
            5 => nonexistent(),
        },
        _ => -1,
    }
}

async fn run_jobs() -> i64 {
    var total = 0;
    const for <Name, Execute, M> in decorated<routes::Job, namespace routes> {
        total += await Execute(M.delta);
    }
    const for <Offset> in [1, 2] {
        total += await yield_now(Offset);
    }
    return total;
}

fn main() {
    const for <N, F, M> in decorated<Handler, namespace routes> {
        assert(decorator_metadata<Handler>(F) == M);
        if M.weight == 1 { print(N, " ", M.weight); }
        else { print(N, " ", M.weight, " ", invoke(F)); }
    }
    const for <N, F, M> in decorated<Handler, namespace peer> {
        print(N, " ", M.weight, " ", F(10));
    }
    const for <N, F, M> in decorated<Handler, namespace ::> {
        print(N, " ", M.weight, " ", F(10));
    }
    const for <N, T, M> in decorated<routes::Entity, namespace routes> {
        assert(decorator_metadata<routes::Entity, T>() == M);
        let value = T<u32> { 19 };
        print(N, " ", M.number, " ", value.value);
    }
    const for <N, T, M> in decorated<routes::Category, namespace routes> {
        let state: T = T.Ready;
        print(N, " ", M.number, " ", state);
    }
    const for <N, T, M> in decorated<routes::Category, namespace unloaded> {
        print(nonexistent());
    }
    let item = Packet { value: 64 };
    const for <N, A, M> in decorated<schemas::Column, Packet> {
        print(N, " ", M.number, " ", item.A);
    }
    let copy = Packet {
        const for <N, A, M> in decorated<schemas::Column, Packet> {
            A: item.A + 1,
        }
    };
    assert(copy.id == 13 && copy.value == 65);
    let message = schemas::Event<i64>.Sent { 9 };
    const for <N, A, M> in decorated<schemas::Column, schemas::Event<i64>.Sent> {
        print(N, " ", M.number, " ", message.A);
    }
    const for <N, A, M> in decorated<schemas::Column, Reordered> {
        print(N, " ", M.number);
    }
    print(lookup(4), " ", lookup(5));
    print("jobs ", run_jobs().join());
}
