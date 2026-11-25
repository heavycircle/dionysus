const std = @import("std");
const build_options = @import("build_options");
const Ctx = @import("Ctx.zig");

const Command = struct {
    name: []const u8,
    summary: []const u8,
    usage: []const u8,
    run: *const fn (*Ctx, []const []const u8) Ctx.Error!u8,
};

const commands = [_]Command{
    command("init", "Create a new project", @import("commands/init.zig")),
    command("build", "Build the project for one or more targets", @import("commands/build.zig")),
    command("run", "Build and run the project's executable", @import("commands/run.zig")),
    command("test", "Build and run the project's tests", @import("commands/test.zig")),
    command("targets", "List supported targets", @import("commands/targets.zig")),
    command("doctor", "Check that required tools are installed", @import("commands/doctor.zig")),
    command("clean", "Remove build directories", @import("commands/clean.zig")),
};

const help_usage =
    \\Usage: dino help [command]
    \\
    \\Show the commands, or how to use one. `dino <command> --help` does the same.
    \\
;

const version_usage =
    \\Usage: dino version
    \\
    \\Print the dino version.
    \\
;

fn command(comptime name: []const u8, comptime summary: []const u8, comptime module: type) Command {
    return .{ .name = name, .summary = summary, .usage = module.usage, .run = module.main };
}

const usage = blk: {
    var text: []const u8 =
        \\dino - cross-compile C projects into tiny static binaries
        \\
        \\Usage: dino <command> [options]
        \\
        \\Commands:
        \\
    ;
    for (commands) |c| text = text ++ std.fmt.comptimePrint("    {s:<10} {s}\n", .{ c.name, c.summary });
    text = text ++
        \\    version    Print the dino version
        \\    help       Show help for a command
        \\
        \\Run `dino help <command>` for details.
        \\
        \\Environment:
        \\    DINO_HOME          Where dino keeps its unpacked SDK (default: user cache dir)
        \\    DINO_SDK_PATH      Build against this SDK checkout instead of the embedded one
        \\    DINO_LIBRARY_PATH  Library directories every project can link (like PATH)
        \\
    ;
    break :blk text;
};

pub fn main(init: std.process.Init) !u8 {
    const arena = init.arena.allocator();
    const args = try init.minimal.args.toSlice(arena);

    // Streaming, not positional: redirected to a file, stdout, stderr and the
    // CMake children all write to it, and positional writes from offset 0 would
    // overwrite each other.
    var stdout_buffer: [4096]u8 = undefined;
    var stdout_writer: std.Io.File.Writer = .initStreaming(.stdout(), init.io, &stdout_buffer);
    var stderr_buffer: [1024]u8 = undefined;
    var stderr_writer: std.Io.File.Writer = .initStreaming(.stderr(), init.io, &stderr_buffer);

    var ctx: Ctx = .{
        .io = init.io,
        .arena = arena,
        .env = init.environ_map,
        .out = &stdout_writer.interface,
        .err = &stderr_writer.interface,
    };
    defer ctx.out.flush() catch {};

    return dispatch(&ctx, args[1..]) catch |err| switch (err) {
        error.Reported => 1,
        error.Usage => 2,
    };
}

fn dispatch(ctx: *Ctx, args: []const []const u8) Ctx.Error!u8 {
    if (args.len == 0) {
        ctx.print("{s}", .{usage});
        return 0;
    }
    const name = args[0];
    if (std.mem.eql(u8, name, "help") or std.mem.eql(u8, name, "-h") or std.mem.eql(u8, name, "--help")) {
        if (args.len > 1) {
            const text = if (std.mem.eql(u8, args[1], "help"))
                help_usage
            else if (std.mem.eql(u8, args[1], "version"))
                version_usage
            else if (find(args[1])) |c|
                c.usage
            else
                return ctx.usage("unknown command '{s}'", .{args[1]});
            ctx.print("{s}", .{text});
        } else {
            ctx.print("{s}", .{usage});
        }
        return 0;
    }
    if (std.mem.eql(u8, name, "version") or std.mem.eql(u8, name, "--version") or std.mem.eql(u8, name, "-V")) {
        ctx.print("dino {s}\n", .{build_options.version});
        return 0;
    }
    const c = find(name) orelse {
        ctx.errorMessage("unknown command '{s}'\n\n{s}", .{ name, usage });
        return 2;
    };
    return c.run(ctx, args[1..]);
}

fn find(name: []const u8) ?Command {
    for (commands) |c| {
        if (std.mem.eql(u8, c.name, name)) return c;
    }
    return null;
}

test {
    _ = @import("Args.zig");
    _ = @import("Ctx.zig");
    _ = @import("cmake.zig");
    _ = @import("compdb.zig");
    _ = @import("libraries.zig");
    _ = @import("sdk_cache.zig");
    _ = @import("targets.zig");
    _ = @import("version.zig");
    _ = @import("commands/build.zig");
    _ = @import("commands/doctor.zig");
    _ = @import("commands/init.zig");
    _ = @import("commands/test.zig");
}
