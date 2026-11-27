// error: which does not outlive the destination
var escaped: const i64? = null;
async fn publish(value: const i64&) {
    await yield_now();
    escaped .= value;
}

fn main() {
    let value = 41;
    publish(value).join();
}
