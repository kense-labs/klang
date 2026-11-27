import cordis;

fn print_error() {
    var error: u8[2048] = [0; 2048];
    let n = cordis::error_json(error[..]);
    assert(n > 0 && n <= error.len);
    print(error[..n]);
}

fn check_result(request: own cordis::InferenceRequest, first: f32, second: f32) {
    defer cordis::close_inference(request);
    let completion = native_task<cordis::InferenceResult>(cordis::inference_task(request)).join();
    assert(completion.status == 0 && completion.written == 2);
    var short: f32[1] = [-10.0];
    assert(cordis::inference_output(request, short[..]) == 0 && short[0] == -10.0);
    var out: f32[2] = [0.0; 2];
    assert(cordis::inference_output(request, out[..]) == 2);
    assert(out[0] == first && out[1] == second);
}

fn main() {
    let host = cordis::open_inference("providers.json", "a.lock");
    defer cordis::close(host);
    if cordis::valid(host) == 0 { print_error(); abort("inference startup failed"); }
    let layout = cordis::inference_layout(host);
    assert(layout.input.rank == 2 && layout.input.elements == 3);
    assert(layout.input.dimensions[0] == 1 && layout.input.dimensions[1] == 3);
    assert(layout.output.rank == 2 && layout.output.elements == 2 && layout.output.dimensions[1] == 2);
    let initial = cordis::inference_workspace(host);
    assert(initial.active_measured == initial.last_activation_measured);
    if initial.active_measured != 0 {
        assert(initial.active.reserved_bytes > 0 && initial.active.reserved_bytes == initial.last_activation_reserved_bytes);
    }
    assert(initial.retired_count == 0);
    var input: f32[3] = [1.0, 2.0, 3.0];
    {
        let wrong = cordis::infer(host, input[..2]);
        defer cordis::close_inference(wrong);
        assert(cordis::valid(wrong) == 0);
        print_error();
    }
    {
        let old = cordis::infer(host, input[..]);
        defer cordis::close_inference(old);
        assert(cordis::valid(old) == 1);
        var old_out: f32[2] = [0.0; 2];
        assert(cordis::inference_output(old, old_out[..]) == 0);
        {
            let busy = cordis::infer(host, input[..]);
            defer cordis::close_inference(busy);
            assert(cordis::valid(busy) == 0);
            print_error();
        }
        input[0] = 100.0;
        task_scope(native_task<cordis::InferenceResult>(cordis::inference_task(old))) { scope =>
            assert(scope.step());
            assert(cordis::select(host, "invalid", "invalid.lock") == -1);
            print_error();
            assert(cordis::inference_workspace(host).last_activation_reserved_bytes == initial.last_activation_reserved_bytes);
            var id: u8[4] = [0; 4];
            assert(cordis::active_provider(host, id[..]) == 1 && id[..1] == "a");
            assert(cordis::select(host, "b", "b.lock") == 1);
            let overlap = cordis::inference_workspace(host);
            assert(overlap.active_measured == initial.active_measured && overlap.last_activation_measured == initial.active_measured);
            assert(overlap.retired_count == 1 && overlap.retired_measured == initial.active_measured);
            if overlap.last_activation_measured != 0 {
                assert(overlap.retired_reserved_bytes >= initial.active.reserved_bytes);
                assert(overlap.last_activation_reserved_bytes >= overlap.active.reserved_bytes + initial.active.reserved_bytes);
            }
            assert(cordis::active_provider(host, id[..]) == 1 && id[..1] == "b");
            input[0] = 1.0;
            check_result(cordis::infer(host, input[..]), 3.0, 6.0);
            let completion = scope.join(0);
            assert(completion.status == 0 && completion.written == 2);
        }
        assert(cordis::inference_output(old, old_out[..]) == 2);
        assert(old_out[0] == 2.0 && old_out[1] == 8.0);
        print("locked weights and hot replacement verified");
    }
    let drained = cordis::inference_workspace(host);
    assert(drained.retired_count == 0 && drained.retired_reserved_bytes == 0);
    assert(drained.active.peak_in_use_bytes >= drained.active.in_use_bytes);
    for i in 0..2 {
        let request = cordis::infer(host, input[..]);
        defer cordis::close_inference(request);
        assert(cordis::valid(request) == 1);
        task_scope(native_task<cordis::InferenceResult>(cordis::inference_task(request))) { scope =>
            if i == 1 { assert(scope.step()); }
            scope.cancel(0);
            assert(!scope.step());
        }
        var cancelled: f32[2] = [-1.0; 2];
        assert(cordis::inference_output(request, cancelled[..]) == 0);
        assert(cancelled[0] == -1.0 && cancelled[1] == -1.0);
    }
    let detached = cordis::infer(host, input[..]);
    let task = cordis::inference_task(detached);
    task_scope(native_task<cordis::InferenceResult>(task)) { scope =>
        assert(scope.step());
        cordis::close_inference(detached);
        scope.cancel(0);
    }
    print("cancelled work returned its request slot");
    check_result(cordis::infer(host, input[..]), 3.0, 6.0);
    let held = cordis::infer(host, input[..]);
    let held_task = cordis::inference_task(held);
    task_scope(native_task<cordis::InferenceResult>(held_task)) { scope =>
        assert(scope.step());
        cordis::close_inference(held);
        assert(cordis::select(host, "a", "a.lock") == 1);
        assert(cordis::inference_workspace(host).retired_count == 1);
        scope.cancel(0);
    }
    assert(cordis::inference_workspace(host).retired_count == 0);
    print("reused slot after draining");
}
