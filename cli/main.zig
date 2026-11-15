const std = @import("std");
const build_options = @import("build_options");
const sdk = @import("sdk");

const usage =
    \\dino - cross-compile C projects into tiny static binaries
    \\
    \\Usage: dino <command> [options]
    \\
    \\Commands:
    \\    help       Show this message
    \\    version    Print the dino version
    \\
;

pub fn main(init: std.process.Init) !void {
    const arena = init.arena.allocator();
    const args = try init.minimal.args.toSlice(arena);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout_writer: std.Io.File.Writer = .init(.stdout(), init.io, &stdout_buffer);
    const stdout = &stdout_writer.interface;
    defer stdout.flush() catch {};

    const command = if (args.len > 1) args[1] else "help";
    if (isHelp(command)) {
        try stdout.writeAll(usage);
    } else if (std.mem.eql(u8, command, "version") or std.mem.eql(u8, command, "--version")) {
        try stdout.print("dino {s}\n", .{build_options.version});
    } else {
        std.log.err("unknown command '{s}'\n\n{s}", .{ command, usage });
        std.process.exit(2);
    }
}

fn isHelp(arg: []const u8) bool {
    for ([_][]const u8{ "help", "-h", "--help" }) |name| {
        if (std.mem.eql(u8, arg, name)) return true;
    }
    return false;
}

test "sdk is embedded" {
    var has_cmake = false;
    var has_toolchain = false;
    for (sdk.files) |file| {
        try std.testing.expect(file.data.len > 0);
        try std.testing.expect(!std.mem.startsWith(u8, file.path, "scripts/"));
        if (std.mem.eql(u8, file.path, "CMakeLists.txt")) has_cmake = true;
        if (std.mem.eql(u8, file.path, "cmake/toolchain.cmake")) has_toolchain = true;
    }
    try std.testing.expect(has_cmake and has_toolchain);
}
