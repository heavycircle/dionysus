//! CMake writes every compile_commands.json entry as `zig cc -target <t> ...`.
//! Clang tools can't parse that: they take `zig` as the compiler and `cc` as
//! an input file, then fall back to the host's own libc headers. The copy dino
//! puts at the project root is rewritten into a plain clang command line that
//! names the target's real musl/MinGW headers, so clangd and clang-tidy see
//! exactly what the build compiles, Windows-only code included.
const std = @import("std");
const builtin = @import("builtin");
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

/// Writes `src` to `dest`, rewritten for clang tools. Entries that aren't a
/// zig compile (a resource compiler, an assembler) are kept as they are. Falls
/// back to a plain copy if the database can't be read or zig can't be probed,
/// so the build never fails over editor support.
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

const RewriteError = error{ UnreadableDatabase, ProbeFailed } || std.mem.Allocator.Error;

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
        const args = entry.arguments orelse if (entry.command) |command|
            if (builtin.os.tag == .windows) try splitWindowsCommand(ctx.arena, command) else try splitCommand(ctx.arena, command)
        else
            return error.UnreadableDatabase;
        o.* = .{ .directory = entry.directory, .file = entry.file, .arguments = args, .output = entry.output };
        const compile = zigCompile(args) orelse continue;
        const probe = probes.get(compile.driver) orelse try probeZig(ctx, target, compile.driver);
        probes.set(compile.driver, probe);
        o.arguments = try rewriteArgs(ctx.arena, args, probe);
    }
    return std.json.Stringify.valueAlloc(ctx.arena, out, .{ .whitespace = .indent_2, .emit_null_optional_fields = false });
}

fn probeZig(ctx: *Ctx, target: []const u8, driver: Driver) RewriteError!Probe {
    // -### prints the clang -cc1 line zig would run, without compiling or
    // reading the input, so stdin stands in for a source file: a project's
    // own may be generated and not exist yet.
    const language = switch (driver) {
        .cc => "c",
        .@"c++" => "c++",
    };
    const result = ctx.capture(&.{ "zig", @tagName(driver), "-target", target, "-###", "-x", language, "-c", "-", "-o", "probe.o" }) catch
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

const ZigCompile = struct { driver: Driver, rest: usize };

/// Where `zig cc`/`zig c++` starts in an entry, after any launcher (ccache,
/// CMAKE_<LANG>_COMPILER_LAUNCHER), or null when the entry isn't one.
fn zigCompile(args: []const []const u8) ?ZigCompile {
    for (0..args.len -| 1) |i| {
        if (!isZig(args[i])) continue;
        const driver = std.meta.stringToEnum(Driver, args[i + 1]) orelse return null;
        return .{ .driver = driver, .rest = i + 2 };
    }
    return null;
}

/// `[launcher] zig cc -target T <rest>` becomes `clang --target=<triple> <probe
/// flags> <rest>`, and `zig c++` becomes `clang++`. Anything else is kept.
pub fn rewriteArgs(arena: std.mem.Allocator, args: []const []const u8, probe: Probe) ![]const []const u8 {
    const compile = zigCompile(args) orelse return args;
    const driver: []const u8 = switch (compile.driver) {
        .cc => "clang",
        .@"c++" => "clang++",
    };

    var out: std.ArrayList([]const u8) = .empty;
    try out.appendSlice(arena, &.{ driver, try std.fmt.allocPrint(arena, "--target={s}", .{probe.triple}) });
    try out.appendSlice(arena, probe.flags);
    var i: usize = compile.rest;
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

/// Splits a shell-quoted command line as CMake writes it on POSIX hosts, and as
/// clang's -### prints it everywhere: whitespace separates arguments, and
/// single quotes, double quotes and backslashes quote.
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

/// Splits a command line as CMake writes it on Windows, by the rules
/// CommandLineToArgvW and the C runtime use: a backslash is literal unless a
/// run of them ends in a double quote, where each pair is one backslash and an
/// odd one escapes the quote.
pub fn splitWindowsCommand(arena: std.mem.Allocator, command: []const u8) ![]const []const u8 {
    var args: std.ArrayList([]const u8) = .empty;
    var current: std.ArrayList(u8) = .empty;
    var in_arg = false;
    var quoted = false;
    var i: usize = 0;
    while (i < command.len) {
        const c = command[i];
        if (!quoted and (c == ' ' or c == '\t' or c == '\n')) {
            if (in_arg) {
                try args.append(arena, try current.toOwnedSlice(arena));
                in_arg = false;
            }
            i += 1;
            continue;
        }
        in_arg = true;
        if (c == '\\') {
            var n: usize = 0;
            while (i < command.len and command[i] == '\\') : (i += 1) n += 1;
            const before_quote = i < command.len and command[i] == '"';
            try current.appendNTimes(arena, '\\', if (before_quote) n / 2 else n);
            if (before_quote and n % 2 == 1) {
                try current.append(arena, '"');
                i += 1;
            }
        } else if (c == '"') {
            // "" inside quotes is a literal quote.
            if (quoted and i + 1 < command.len and command[i + 1] == '"') {
                try current.append(arena, '"');
                i += 2;
            } else {
                quoted = !quoted;
                i += 1;
            }
        } else {
            try current.append(arena, c);
            i += 1;
        }
    }
    if (in_arg) try args.append(arena, try current.toOwnedSlice(arena));
    return args.items;
}

test splitWindowsCommand {
    var arena_state: std.heap.ArenaAllocator = .init(std.testing.allocator);
    defer arena_state.deinit();
    const a = arena_state.allocator();
    const expectArgs = struct {
        fn f(alloc: std.mem.Allocator, expected: []const []const u8, line: []const u8) !void {
            const got = try splitWindowsCommand(alloc, line);
            try std.testing.expectEqual(expected.len, got.len);
            for (expected, got) |e, g| try std.testing.expectEqualStrings(e, g);
        }
    }.f;
    try expectArgs(a, &.{ "C:\\zig\\zig.exe", "cc", "-IC:\\proj\\src", "-c", "C:\\proj\\src\\main.c" }, "C:\\zig\\zig.exe cc  -IC:\\proj\\src -c C:\\proj\\src\\main.c");
    try expectArgs(a, &.{ "C:\\Program Files\\zig\\zig.exe", "-DMSG=\"hi there\"" }, "\"C:\\Program Files\\zig\\zig.exe\" \"-DMSG=\\\"hi there\\\"\"");
    try expectArgs(a, &.{ "a\\b c", "d\\\\", "x\\y z", "e\"f" }, "\"a\\b c\" d\\\\ x\\\\\"y z\" \"e\"\"f\"");
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
    // An empty quoted argument is still an argument.
    try expectArgs(a, &.{ "zig", "", "-c" }, "zig \"\" -c");
    // Malformed input never fails: an unterminated quote runs to the end, and
    // a trailing backslash stays.
    try expectArgs(a, &.{ "zig", "-DX=a b" }, "zig \"-DX=a b");
    try expectArgs(a, &.{ "zig", "a\\" }, "zig a\\");
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
    const launched = try rewriteArgs(a, &.{ "/usr/bin/ccache", "/opt/zig/zig", "cc", "-target", "x86_64-linux-musl", "-O0", "-c", "a.c" }, probe);
    try std.testing.expectEqual(expected.len, launched.len);
    for (expected, launched) |e, g| try std.testing.expectEqualStrings(e, g);
    const rc = [_][]const u8{ "zig", "rc", "/fo", "app.res", "app.rc" };
    try std.testing.expectEqual(@as(usize, rc.len), (try rewriteArgs(a, &rc, probe)).len);
    try std.testing.expectEqualStrings("gcc", (try rewriteArgs(a, &.{ "gcc", "-c", "a.c" }, probe))[0]);
}
