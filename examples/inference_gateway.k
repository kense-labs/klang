import binary;
import cordis;
import os;

const ok: i32 = 0;
const data: u8 = 1;
const done: i32 = 2;
const bad_request: i32 = 3;
const inference_error: i32 = 6;
const closed: i32 = 7;
const max_elements: u32 = 1048576;
const batch_elements = 1024;

struct Reload {
    marker: const u8[:],
    provider: const u8[:],
    lock: const u8[:],
    attempted: bool,
}

fn print_diagnostic() {
    var buffer: u8[4096] = [0; 4096];
    let size = cordis::error_json(buffer[..]);
    if size > 0 && size <= buffer.len { print(buffer[..size]); }
}

fn reload_due(reload: Reload&) -> bool {
    if reload.attempted || reload.marker.len == 0 || !file_exists(reload.marker) { return false; }
    reload.attempted = true;
    return true;
}

fn show_selection(result: i32, provider: const u8[:]) {
    if result < 0 {
        print_diagnostic();
        print("reload rejected");
    } else {
        print("selected ", provider);
    }
    flush_stdout();
}

fn show_workspace(phase: const u8[:], usage: cordis::InferenceWorkspace) {
    print("workspace ", phase, " {\"active_measured\":", usage.active_measured,
          ",\"active_reserved_bytes\":", usage.active.reserved_bytes,
          ",\"active_in_use_bytes\":", usage.active.in_use_bytes,
          ",\"active_peak_in_use_bytes\":", usage.active.peak_in_use_bytes,
          ",\"retired_count\":", usage.retired_count, ",\"retired_measured\":", usage.retired_measured,
          ",\"retired_reserved_bytes\":", usage.retired_reserved_bytes,
          ",\"last_activation_measured\":", usage.last_activation_measured,
          ",\"last_activation_reserved_bytes\":", usage.last_activation_reserved_bytes, "}");
    flush_stdout();
}

fn decode_batch(bytes: const u8[:], input: f32[>..]&) {
    for i in bytes.len / 4 {
        let value, valid = binary::read_f32le(bytes, i * 4);
        assert(valid);
        input.push(value);
    }
}

fn encode_frame(frame: u8[>..]&, values: const f32[:]) {
    binary::write_u8(frame, data);
    binary::write_u32le(frame, (values.len * 4) as u32);
    for value in values { binary::write_f32le(frame, value); }
}

fn option(value: const u8[:], maximum: u32) -> u32 {
    let number, valid = parse_uint(value);
    if !valid || number > maximum { abort("invalid gateway option"); }
    return number as u32;
}

fn main() {
    let argv = args();
    var workspace = false;
    var memory_mib: u32 = 0;
    var count_args = argv.len;
    while count_args > 9 {
        if argv[count_args - 1] == "--workspace" {
            if workspace { abort("duplicate --workspace"); }
            workspace = true;
            count_args--;
        } else if count_args >= 11 && argv[count_args - 2] == "--memory-budget-mib" {
            if memory_mib != 0 { abort("duplicate --memory-budget-mib"); }
            memory_mib = option(argv[count_args - 1], 4294967295);
            if memory_mib == 0 { abort("memory budget must be positive"); }
            count_args -= 2;
        } else {
            break;
        }
    }
    if count_args != 9 && count_args != 12 {
        abort("usage: gateway MANIFEST LOCK PORT PENDING DEADLINE_MS MAX_ELEMENTS CHUNK_ELEMENTS REQUESTS [MARKER PROVIDER LOCK] [--workspace] [--memory-budget-mib MIB]");
    }
    let port = option(argv[3], 65535);
    let pending = option(argv[4], 1024);
    let budget = option(argv[5], 3600000);
    let limit = option(argv[6], max_elements);
    let chunk = option(argv[7], batch_elements);
    let requests = option(argv[8], 4294967295);
    if limit == 0 || chunk == 0 || pending == 0 || budget == 0 { abort("gateway limits must be positive"); }
    if memory_mib != 0 && cordis::require_memory_budget((memory_mib as u64) * 1048576) == 0 {
        print_diagnostic();
        abort("gateway memory budget verification failed");
    }
    var reload = Reload { "", "", "", false };
    if count_args == 12 { reload = Reload { argv[9], argv[10], argv[11], false }; }
    let host = cordis::open_inference(argv[1], argv[2]);
    defer cordis::close(host);
    if cordis::valid(host) == 0 { print_diagnostic(); abort("gateway inference startup failed"); }
    let listener = cordis::gateway_listen(port, pending, budget);
    defer cordis::gateway_close(listener);
    let bound = cordis::gateway_port(listener);
    if bound == 0 { abort("gateway bind failed"); }
    print("ready ", bound);
    flush_stdout();
    if workspace { show_workspace("startup", cordis::inference_workspace(host)); }
    var served: u32 = 0;
    while requests == 0 || served < requests {
        let connection = cordis::gateway_accept(listener);
        defer cordis::gateway_close(connection);
        if cordis::valid(connection) == 0 { abort("gateway accept stopped"); }
        let status = block {
            var header: u8[8] = [0; 8];
            let header_status = cordis::gateway_read(connection, header[..]);
            if header_status != ok { break header_status; }
            let count, valid = binary::read_u32le(header[..], 4);
            if !valid || header[..4] != "KIG1" || count == 0 || count > limit { break bad_request; }
            if reload_due(reload) {
                show_selection(cordis::select(host, reload.provider, reload.lock), reload.provider);
                if workspace { show_workspace("reload", cordis::inference_workspace(host)); }
            }
            let layout = cordis::inference_layout(host);
            if count != layout.input.elements { break bad_request; }
            if layout.output.elements == 0 || layout.output.elements > limit { break inference_error; }

            var input: f32[>..] = [];
            var receive_status = ok;
            {
                var bytes: u8[4096] = [0; 4096];
                while input.len < count {
                    let n = min(count - input.len, batch_elements);
                    receive_status = cordis::gateway_read(connection, bytes[..n * 4]);
                    if receive_status != ok { break; }
                    decode_batch(bytes[..n * 4], input);
                }
            }
            if receive_status != ok { break receive_status; }
            let received = cordis::gateway_peer(connection);
            if received != ok { break received; }

            let request = cordis::infer(host, input[..]);
            defer cordis::close_inference(request);
            if cordis::valid(request) == 0 { break inference_error; }
            let task = cordis::inference_task(request);
            if cordis::valid(task) == 0 {
                cordis::discard_task(task);
                break inference_error;
            }
            var status = ok;
            task_scope(native_task<cordis::InferenceResult>(task)) { scope =>
                scope.step();
                while !scope.finished(0) {
                    status = cordis::gateway_peer(connection);
                    if status != ok { break; }
                    if reload_due(reload) {
                        show_selection(cordis::select(host, reload.provider, reload.lock), reload.provider);
                        if workspace { show_workspace("reload", cordis::inference_workspace(host)); }
                    }
                    scope.wait_for(10);
                }
                if status != ok {
                    scope.cancel(0);
                } else {
                    let result = scope.join(0);
                    if result.status != 0 || result.written != layout.output.elements { status = inference_error; }
                }
            }
            if status != ok { break status; }
            let completed = cordis::gateway_peer(connection);
            if completed != ok { break completed; }

            var output: f32[>..] = [];
            output.push_n(0.0, layout.output.elements);
            if cordis::inference_output(request, output[..]) != layout.output.elements { break inference_error; }
            var frame: u8[>..] = [];
            var offset = 0;
            var write_status = ok;
            while offset < output.len {
                let n = min(output.len - offset, chunk);
                encode_frame(frame, output[offset..offset + n]);
                write_status = cordis::gateway_write(connection, frame[..]);
                if write_status != ok { break; }
                frame.clear();
                offset += n;
            }
            if write_status != ok { break write_status; }
            done
        };
        if status != closed { cordis::gateway_finish(connection, status as u8); }
        if requests != 0 { served++; }
    }
    if workspace { show_workspace("shutdown", cordis::inference_workspace(host)); }
    print("peak pending ", cordis::gateway_peak_pending(listener));
}
