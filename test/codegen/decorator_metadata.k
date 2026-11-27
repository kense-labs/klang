import .decorator_support.routes;
namespace catalog = routes;

decorator Marker() on fn;
decorator AsyncRoute(name: const u8[:]) on async fn(i64) -> i64;

const Provider: const u8[:] = "v2";
const Enabled:  bool        = true;
var calls: i64 = 0;

@Marker
@catalog::Route(Provider, 1 + 2, Enabled)
fn handler(input: i64) -> i64 {
    calls += 1;
    return input + 1;
}

fn handler(input: f64) -> f64 { input }

@Marker
fn identity<T>(value: T) -> T { value }

@AsyncRoute("pending")
async fn run_async(value: i64) -> i64 {
    await yield_now();
    return identity(value);
}

let registration = decorator_metadata<catalog::Route>(handler);

fn read_metadata<Target>() -> catalog::Route {
    return decorator_metadata<catalog::Route>(Target);
}

fn main() {
    assert(decorator_metadata<Marker>(identity) == Marker {});
    assert(decorator_metadata<Marker>(handler) == Marker {});
    let copied = read_metadata(handler);
    assert(copied == registration && copied.enabled && calls == 0);
    print(copied.provider, " ", copied.capacity, " ", handler(4), " ", calls);
    let async_metadata = decorator_metadata<AsyncRoute>(run_async);
    print(async_metadata.name, " ", run_async(7).join());
}
