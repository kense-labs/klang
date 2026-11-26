// Keep command-line containers outside compilation. Each invocation receives
// only the operation and borrowed paths, and owns a fresh Compiler context.
const COMMAND_PARSE = 0;
const COMMAND_CHECK = 1;
const COMMAND_TOKENS = 2;
const COMMAND_EMIT = 3;

fn run_commands() {
    var argument_storage: u8[>..] = [];
    let arguments = args(argument_storage);
    if arguments.len == 2 && arguments[1] == "--help" {
        print("usage: klang-klang [--tokens | --parse | --check | -o output.c] input.k");
        print("       klang-klang --check-many input.k ...");
        return;
    }
    if arguments.len > 2 && arguments[1] == "--check-many" {
        for i in 2..arguments.len { run_compilation(COMMAND_CHECK, arguments[i], ""); }
        return;
    }
    if arguments.len == 2 && arguments[1].len > 0 && arguments[1][0] != '-' {
        run_compilation(COMMAND_PARSE, arguments[1], ""); return;
    }
    if arguments.len == 4 && arguments[1] == "-o" {
        run_compilation(COMMAND_EMIT, arguments[3], arguments[2]); return;
    }
    if arguments.len == 3 {
        var mode = -1;
        if arguments[1] == "--parse" { mode = COMMAND_PARSE; }
        if arguments[1] == "--check" { mode = COMMAND_CHECK; }
        if arguments[1] == "--tokens" { mode = COMMAND_TOKENS; }
        if mode >= 0 { run_compilation(mode, arguments[2], ""); return; }
    }
    write_stderr("usage: klang-klang [--tokens | --parse | --check | -o output.c] input.k\n");
    exit(2);
}
