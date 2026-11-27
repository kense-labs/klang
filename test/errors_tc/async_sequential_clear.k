// error: cannot clear right while view is still used
async fn invalid() {
    var left: u8[][>..] = ["left"];
    var right: u8[][>..] = ["0", "right"];
    var view = left[..];
    for word, index in view {
        if index == 0 { view = right[..]; continue; }
        view = left[..];
        right.clear();
        await yield_now();
        print(word);
    }
}

fn main() { invalid().join(); }
