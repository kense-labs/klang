// Hosted Cordis application API. Deploy the optional host library under
// lib/ in the program's resource directory.
namespace cordis;

struct Host { token: u64 }
struct TcpSocket { token: u64 }
struct StreamReader { token: u64 }
struct StreamTask { token: u64 }
struct ReadResult { status: i32, error: i32, count: u32 }
struct StreamStats { submitted: u64, queued: u32, reading: u32 }
struct InferenceRequest { token: u64 }
struct InferenceTask { token: u64 }
struct TensorInfo { rank: u32, elements: u32, dimensions: i64[8] }
struct InferenceLayout { input: TensorInfo, output: TensorInfo }
struct InferenceResult { status: i32, written: u32 }
struct InferenceArena { reserved_bytes: u64, in_use_bytes: u64, peak_in_use_bytes: u64 }
struct InferenceWorkspace {
    active:                         InferenceArena,
    retired_reserved_bytes:         u64,
    last_activation_reserved_bytes: u64,
    active_measured:                u32,
    retired_count:                  u32,
    retired_measured:               u32,
    last_activation_measured:       u32,
}

struct GatewayListener { token: u64 }
struct GatewayConnection { token: u64 }

extern "lib/k_cordis_application" {
    fn open(manifest: const u8[:], lock: const u8[:]) -> own Host = "k_cordis_app_open_v1";
    fn select(borrow host: Host, provider: const u8[:], lock: const u8[:]) -> i32 = "k_cordis_app_select_v1";
    fn close(host: own Host) = "k_cordis_app_close_v1";
    fn valid(borrow host: Host) -> i32 = "k_cordis_app_ready_v1";
    fn require_memory_budget(bytes: u64) -> i32 = "k_cordis_app_require_memory_budget_v1";

    // The text functions return the required length and write only a
    // complete result. A zero-length output can query the length.
    fn error_json(out: u8[:]) -> i64 = "k_cordis_app_error_v1";
    fn active_graph(borrow host: Host, out: u8[:]) -> i64 = "k_cordis_app_graph_v1";
    fn active_provider(borrow host: Host, out: u8[:]) -> i64 = "k_cordis_app_provider_v1";
    fn stream_provider(borrow reader: StreamReader, out: u8[:]) -> i64 = "k_cordis_app_reader_provider_v1";

    // Adopt consumes the native socket on either outcome. A zero token is
    // a failed open, adopt or pump. The matching close/discard accepts it.
    fn adopt_tcp(borrow host: Host, socket: own TcpSocket, capacity: u32) -> own StreamReader = "k_cordis_app_adopt_v1";
    fn pump(borrow reader: StreamReader) -> own StreamTask = "k_cordis_app_pump_v1";
    fn valid(borrow reader: StreamReader) -> i32 = "k_cordis_app_reader_ready_v1";
    fn valid(borrow task: StreamTask) -> i32 = "k_cordis_app_task_ready_v1";
    fn take(borrow reader: StreamReader, out: u8[:]) -> ReadResult = "k_cordis_app_take_v1";
    fn stats(borrow reader: StreamReader) -> StreamStats = "k_cordis_app_stats_v1";
    fn close_reader(reader: own StreamReader) = "k_cordis_app_close_reader_v1";
    fn discard_task(task: own StreamTask) = "k_cordis_app_discard_task_v1";

    // Inference can be the graph root or a dependency of a hosted service.
    fn open_inference(manifest: const u8[:], lock: const u8[:]) -> own Host = "k_cordis_app_open_inference_v1";
    fn inference_layout(borrow host: Host) -> InferenceLayout = "k_cordis_app_inference_layout_v1";
    fn inference_workspace(borrow host: Host) -> InferenceWorkspace = "k_cordis_app_inference_workspace_v1";
    fn infer(borrow host: Host, input: const f32[:]) -> own InferenceRequest = "k_cordis_app_infer_v1";
    fn valid(borrow request: InferenceRequest) -> i32 = "k_cordis_app_inference_ready_v1";
    fn inference_task(borrow request: InferenceRequest) -> own InferenceTask = "k_cordis_app_inference_task_v1";
    fn valid(borrow task: InferenceTask) -> i32 = "k_cordis_app_inference_task_ready_v1";
    fn discard_task(task: own InferenceTask) = "k_cordis_app_discard_inference_task_v1";
    fn inference_output(borrow request: InferenceRequest, out: f32[:]) -> u32 = "k_cordis_app_inference_output_v1";
    fn close_inference(request: own InferenceRequest) = "k_cordis_app_close_inference_v1";

    // Optional loopback transport for the reference inference gateway.
    fn gateway_listen(port: u32, pending: u32, budget_ms: u32) -> own GatewayListener = "k_gateway_listen_v1";
    fn gateway_port(borrow listener: GatewayListener) -> u32 = "k_gateway_port_v1";
    fn gateway_accept(borrow listener: GatewayListener) -> own GatewayConnection = "k_gateway_accept_v1";
    fn valid(borrow connection: GatewayConnection) -> i32 = "k_gateway_connected_v1";
    fn gateway_peak_pending(borrow listener: GatewayListener) -> u32 = "k_gateway_peak_pending_v1";
    fn gateway_close(listener: own GatewayListener) = "k_gateway_close_listener_v1";
    fn gateway_peer(borrow connection: GatewayConnection) -> i32 = "k_gateway_peer_status_v1";
    fn gateway_read(borrow connection: GatewayConnection, bytes: u8[:]) -> i32 = "k_gateway_read_v1";
    fn gateway_write(borrow connection: GatewayConnection, bytes: const u8[:]) -> i32 = "k_gateway_write_v1";
    fn gateway_finish(borrow connection: GatewayConnection, status: u8) = "k_gateway_finish_v1";
    fn gateway_close(connection: own GatewayConnection) = "k_gateway_close_connection_v1";
}
