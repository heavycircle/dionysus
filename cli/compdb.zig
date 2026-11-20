//! CMake writes every compile_commands.json entry as `zig cc -target <t> ...`.
//! Clang tools can't parse that: they take `zig` as the compiler and `cc` as
//! an input file, then fall back to the host's own libc headers. The copy dino
//! puts at the project root is rewritten into a plain clang command line that
//! names the target's real musl/MinGW headers, so clangd and clang-tidy see
//! exactly what the build compiles, Windows-only code included.
const std = @import("std");
const Io = std.Io;
const Ctx = @import("Ctx.zig");

/// What `zig cc -###` adds for a target on top of the user's flags.
pub const Probe = struct {
    triple: []const u8,
    flags: []const []const u8,
};

const Entry = struct {
    directory: []const u8,
    file: []const u8,
    command: ?[]const u8 = null,
    arguments: ?[]const []const u8 = null,
    output: ?[]const u8 = null,
};

const OutEntry = struct {
    directory: []const u8,
    file: []const u8,
    arguments: []const []const u8,
    output: ?[]const u8,
};

/// Writes `src` to `dest`, rewritten for clang tools. Falls back to a plain
/// copy if anything about the database or zig's output is unexpected, so the
/// build never fails over editor support.
pub fn write(ctx: *Ctx, src: []const u8, dest: []const u8, target: []const u8) void {
    const cwd = Io.Dir.cwd();
    const rewritten = rewriteFile(ctx, src, target) catch |e| {
        ctx.warn("compile_commands.json not rewritten for clang tools ({t}); copying it as is", .{e});
        cwd.copyFile(src, cwd, dest, ctx.io, .{}) catch |ce|
            ctx.warn("could not copy compile_commands.json: {t}", .{ce});
        return;
    };
    cwd.writeFile(ctx.io, .{ .sub_path = dest, .data = rewritten }) catch |e|
        ctx.warn("could not write {s}: {t}", .{ dest, e });
}

const RewriteError = error{ UnreadableDatabase, ProbeFailed, UnexpectedEntry } || std.mem.Allocator.Error;

fn rewriteFile(ctx: *Ctx, src: []const u8, target: []const u8) RewriteError![]const u8 {
    const bytes = Io.Dir.cwd().readFileAlloc(ctx.io, src, ctx.arena, .limited(64 << 20)) catch
        return error.UnreadableDatabase;
    const entries = std.json.parseFromSliceLeaky([]Entry, ctx.arena, bytes, .{ .ignore_unknown_fields = true }) catch
        return error.UnreadableDatabase;
    if (entries.len == 0) return bytes;

    // C and C++ need their own probe: only `zig c++` adds libc++'s headers.
    var probes: std.EnumArray(Driver, ?Probe) = .initFill(null);
    const out = try ctx.arena.alloc(OutEntry, entries.len);
    for (entries, out) |entry, *o| {
        const args = entry.arguments orelse try splitCommand(ctx.arena, entry.command orelse return error.UnexpectedEntry);
        const driver = try driverOf(args);
        const probe = probes.get(driver) orelse try probeZig(ctx, target, driver, entry.file);
        probes.set(driver, probe);
        o.* = .{
            .directory = entry.directory,
            .file = entry.file,
            .arguments = try rewriteArgs(ctx.arena, args, probe),
            .output = entry.output,
        };
    }
    return std.json.Stringify.valueAlloc(ctx.arena, out, .{ .whitespace = .indent_2, .emit_null_optional_fields = false });
}

fn probeZig(ctx: *Ctx, target: []const u8, driver: Driver, file: []const u8) RewriteError!Probe {
    // -### prints the clang -cc1 line zig would run, without compiling. Any
    // existing source file in the language will do as input.
    const result = ctx.capture(&.{ "zig", @tagName(driver), "-target", target, "-###", "-c", file, "-o", "probe.o" }) catch
        return error.ProbeFailed;
    if (result.code != 0) return error.ProbeFailed;
    return parseProbe(ctx.arena, result.stderr) catch error.ProbeFailed;
}

/// Pulls the target triple, libc include paths and defines out of `zig cc -###`.
pub fn parseProbe(arena: std.mem.Allocator, stderr: []const u8) !Probe {
    var lines = std.mem.splitScalar(u8, stderr, '\n');
    const cc1 = while (lines.next()) |line| {
        if (std.mem.indexOf(u8, line, "\"-cc1\"") != null) break line;
    } else return error.NoCc1Line;

    const args = try splitCommand(arena, cc1);
    var triple: ?[]const u8 = null;
    // -nostdlibinc drops the host's libc headers but keeps each tool's own
    // builtin headers (stddef.h, intrinsics), which must match its clang version.
    var flags: std.ArrayList([]const u8) = .empty;
    try flags.append(arena, "-nostdlibinc");
    for (args[0..args.len -| 1], args[1..]) |flag, value| {
        if (std.mem.eql(u8, flag, "-triple")) triple = value;
        if (std.mem.eql(u8, flag, "-isystem")) {
            // zig's own clang builtin headers; see -nostdlibinc above.
            if (std.mem.endsWith(u8, value, "/lib/include") or std.mem.endsWith(u8, value, "\\lib\\include")) continue;
            try flags.appendSlice(arena, &.{ "-isystem", value });
        } else if (std.mem.eql(u8, flag, "-D")) {
            try flags.append(arena, try std.fmt.allocPrint(arena, "-D{s}", .{value}));
        }
    }
    return .{ .triple = triple orelse return error.NoTriple, .flags = flags.items };
}

/// The zig subcommand an entry compiles with, named as on the command line.
const Driver = enum { cc, @"c++" };

fn driverOf(args: []const []const u8) error{UnexpectedEntry}!Driver {
    if (args.len < 2 or !isZig(args[0])) return error.UnexpectedEntry;
    return std.meta.stringToEnum(Driver, args[1]) orelse error.UnexpectedEntry;
}

/// `zig cc -target T <rest>` becomes `clang --target=<triple> <probe flags> <rest>`,
/// and `zig c++` becomes `clang++`.
pub fn rewriteArgs(arena: std.mem.Allocator, args: []const []const u8, probe: Probe) ![]const []const u8 {
    const driver: []const u8 = switch (try driverOf(args)) {
        .cc => "clang",
        .@"c++" => "clang++",
    };

    var out: std.ArrayList([]const u8) = .empty;
    try out.appendSlice(arena, &.{ driver, try std.fmt.allocPrint(arena, "--target={s}", .{probe.triple}) });
    try out.appendSlice(arena, probe.flags);
    var i: usize = 2;
    while (i < args.len) : (i += 1) {
        if (std.mem.eql(u8, args[i], "-target")) {
            i += 1;
            continue;
        }
        try out.append(arena, args[i]);
    }
    return out.items;
}

fn isZig(path: []const u8) bool {
    const base = std.fs.path.basename(path);
    return std.mem.eql(u8, base, "zig") or std.ascii.eqlIgnoreCase(base, "zig.exe");
}

/// Splits a shell-quoted command line as CMake writes it: whitespace separates
/// arguments, and single quotes, double quotes and backslashes quote.
pub fn splitCommand(arena: std.mem.Allocator, command: []const u8) ![]const []const u8 {
    var args: std.ArrayList([]const u8) = .empty;
    var current: std.ArrayList(u8) = .empty;
    var in_arg = false;
    var quote: ?u8 = null;
    var i: usize = 0;
    while (i < command.len) : (i += 1) {
        const c = command[i];
        if (quote) |q| {
            if (c == q) {
                quote = null;
            } else if (c == '\\' and q == '"' and i + 1 < command.len and std.mem.indexOfScalar(u8, "\"\\$`", command[i + 1]) != null) {
                i += 1;
                try current.append(arena, command[i]);
            } else {
                try current.append(arena, c);
            }
        } else if (c == ' ' or c == '\t' or c == '\n') {
            if (in_arg) {
                try args.append(arena, try current.toOwnedSlice(arena));
                in_arg = false;
            }
        } else {
            in_arg = true;
            if (c == '\'' or c == '"') {
                quote = c;
            } else if (c == '\\' and i + 1 < command.len) {
                i += 1;
                try current.append(arena, command[i]);
            } else {
                try current.append(arena, c);
            }
        }
    }
    if (in_arg) try args.append(arena, try current.toOwnedSlice(arena));
    return args.items;
}

test splitCommand {
    var arena_state: std.heap.ArenaAllocator = .init(std.testing.allocator);
    defer arena_state.deinit();
    const a = arena_state.allocator();
    const expectArgs = struct {
        fn f(alloc: std.mem.Allocator, expected: []const []const u8, line: []const u8) !void {
            const got = try splitCommand(alloc, line);
            try std.testing.expectEqual(expected.len, got.len);
            for (expected, got) |e, g| try std.testing.expectEqualStrings(e, g);
        }
    }.f;
    try expectArgs(a, &.{ "/opt/zig/zig", "cc", "-target", "x86_64-linux-musl", "-c", "a.c" }, "/opt/zig/zig cc -target x86_64-linux-musl  -c a.c");
    try expectArgs(a, &.{ "zig", "-DMSG=hello world", "-I/my dir" }, "zig \"-DMSG=hello world\" -I/my\\ dir");
    try expectArgs(a, &.{ "zig", "-DQ=\"x\"", "it's" }, "zig -DQ=\\\"x\\\" 'it'\\''s'");
}

test parseProbe {
    var arena_state: std.heap.ArenaAllocator = .init(std.testing.allocator);
    defer arena_state.deinit();
    const a = arena_state.allocator();
    const stderr =
        \\zig version 0.17.1
        \\ "/opt/zig/zig" "-cc1" "-triple" "x86_64-unknown-windows-gnu" "-isystem" "/opt/zig/lib/include" "-isystem" "/opt/zig/lib/libc/include/x86_64-windows-gnu" "-isystem" "/opt/zig/lib/libc/include/any-windows-any" "-D" "_WIN32_WINNT=0x0a00" "-o" "probe.o"
        \\
    ;
    const probe = try parseProbe(a, stderr);
    try std.testing.expectEqualStrings("x86_64-unknown-windows-gnu", probe.triple);
    const expected = [_][]const u8{
        "-nostdlibinc",
        "-isystem",
        "/opt/zig/lib/libc/include/x86_64-windows-gnu",
        "-isystem",
        "/opt/zig/lib/libc/include/any-windows-any",
        "-D_WIN32_WINNT=0x0a00",
    };
    try std.testing.expectEqual(expected.len, probe.flags.len);
    for (expected, probe.flags) |e, g| try std.testing.expectEqualStrings(e, g);
    try std.testing.expectError(error.NoCc1Line, parseProbe(a, "no compile line here"));
}

test rewriteArgs {
    var arena_state: std.heap.ArenaAllocator = .init(std.testing.allocator);
    defer arena_state.deinit();
    const a = arena_state.allocator();
    const probe: Probe = .{ .triple = "x86_64-unknown-linux-musl", .flags = &.{ "-nostdlibinc", "-isystem", "/libc" } };
    const got = try rewriteArgs(a, &.{ "/opt/zig/zig", "cc", "-target", "x86_64-linux-musl", "-O0", "-c", "a.c" }, probe);
    const expected = [_][]const u8{ "clang", "--target=x86_64-unknown-linux-musl", "-nostdlibinc", "-isystem", "/libc", "-O0", "-c", "a.c" };
    try std.testing.expectEqual(expected.len, got.len);
    for (expected, got) |e, g| try std.testing.expectEqualStrings(e, g);
    try std.testing.expectEqualStrings("clang++", (try rewriteArgs(a, &.{ "zig", "c++", "-c", "a.cpp" }, probe))[0]);
    try std.testing.expectError(error.UnexpectedEntry, rewriteArgs(a, &.{ "gcc", "-c", "a.c" }, probe));
    try std.testing.expectError(error.UnexpectedEntry, rewriteArgs(a, &.{ "zig", "ar", "a.o" }, probe));
}
