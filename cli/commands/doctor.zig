const std = @import("std");
const builtin = @import("builtin");
const Ctx = @import("../Ctx.zig");
const Args = @import("../Args.zig");
const sdk_cache = @import("../sdk_cache.zig");
const libraries = @import("../libraries.zig");
const targets = @import("../targets.zig");

pub const usage =
    \\Usage: dino doctor
    \\
    \\Check that the tools dino needs are installed and that every library
    \\in DINO_LIBRARY_PATH can be used. Exits non-zero if a required tool is
    \\missing or a library entry is broken; emulators are optional and only
    \\needed by `dino run` for foreign targets.
    \\
;

const min_cmake: std.SemanticVersion = .{ .major = 3, .minor = 15, .patch = 0 };

pub fn main(ctx: *Ctx, raw: []const []const u8) Ctx.Error!u8 {
    for (raw) |arg| {
        if (Args.isHelp(arg)) {
            ctx.print("{s}", .{usage});
            return 0;
        }
        return ctx.usage("unknown argument '{s}'\n\n{s}", .{ arg, usage });
    }
    var problems: usize = 0;

    ctx.print("Required:\n", .{});
    if (ctx.findProgram("zig")) |zig| {
        report(ctx, true, "zig", ctx.sprint("{s} ({s})", .{ firstLine(try versionOf(ctx, &.{ zig, "version" })), zig }));
    } else {
        report(ctx, false, "zig", "not found; download from https://ziglang.org/download/");
        problems += 1;
    }
    if (ctx.findProgram("cmake")) |cmake| {
        const line = firstLine(try versionOf(ctx, &.{ cmake, "--version" }));
        const ok = cmakeVersionOk(line);
        report(ctx, ok, "cmake", if (ok) ctx.sprint("{s} ({s})", .{ line, cmake }) else ctx.sprint("{s}; 3.15 or newer is required", .{line}));
        if (!ok) problems += 1;
    } else {
        report(ctx, false, "cmake", "not found; install CMake 3.15 or newer");
        problems += 1;
    }
    if (ctx.findProgram("ninja")) |ninja| {
        report(ctx, true, "generator", ctx.sprint("ninja ({s})", .{ninja}));
    } else if (builtin.os.tag != .windows and (ctx.findProgram("make") orelse ctx.findProgram("gmake")) != null) {
        report(ctx, true, "generator", "make (install ninja for faster builds)");
    } else {
        report(ctx, false, "generator", if (builtin.os.tag == .windows) "ninja not found (required on Windows)" else "neither ninja nor make found");
        problems += 1;
    }
    if (sdk_cache.path(ctx)) |sdk| {
        report(ctx, true, "sdk", sdk);
    } else |_| {
        problems += 1;
    }

    ctx.print("\nLibraries ({s}):\n", .{libraries.env_var});
    const libs = libraries.inspect(ctx);
    if (libs.len == 0) ctx.print("  none\n", .{});
    for (libs) |lib| {
        const name = std.fs.path.basename(lib.path orelse lib.entry);
        if (lib.problem) |p| {
            report(ctx, false, name, ctx.sprint("'{s}' {s}", .{ lib.entry, p }));
            problems += 1;
        } else {
            report(ctx, true, name, lib.path.?);
        }
    }

    ctx.print("\nRunners for `dino run` (optional):\n", .{});
    for (targets.known) |t| {
        switch (targets.runnerFor(t)) {
            .native => report(ctx, true, t.name, "native"),
            .qemu => |name| if (ctx.findProgram(ctx.sprint("{s}-static", .{name})) orelse ctx.findProgram(name)) |p|
                report(ctx, true, t.name, p)
            else
                report(ctx, false, t.name, ctx.sprint("{s} not found (qemu-user)", .{name})),
            .wine => if (ctx.findProgram("wine")) |p| report(ctx, true, t.name, p) else report(ctx, false, t.name, "wine not found"),
            .unsupported => report(ctx, false, t.name, "cannot run on this host"),
        }
    }

    ctx.print("\n", .{});
    if (problems == 0) {
        ctx.print("All required tools found.\n", .{});
        return 0;
    }
    ctx.print("{d} problem(s) found.\n", .{problems});
    return 1;
}

fn report(ctx: *Ctx, ok: bool, name: []const u8, detail: []const u8) void {
    ctx.print("  [{s}] {s:<10} {s}\n", .{ if (ok) " ok " else "MISS", name, detail });
}

fn versionOf(ctx: *Ctx, argv: []const []const u8) Ctx.Error![]const u8 {
    const result = try ctx.capture(argv);
    return std.mem.trim(u8, result.stdout, " \r\n");
}

fn firstLine(text: []const u8) []const u8 {
    return text[0 .. std.mem.indexOfScalar(u8, text, '\n') orelse text.len];
}

/// Parses "cmake version 3.31.6" (and vendor suffixes like "-dirty").
fn cmakeVersionOk(line: []const u8) bool {
    const prefix = "cmake version ";
    if (!std.mem.startsWith(u8, line, prefix)) return false;
    var it = std.mem.tokenizeAny(u8, line[prefix.len..], ".-+ ");
    const major = std.fmt.parseInt(u32, it.next() orelse return false, 10) catch return false;
    const minor = std.fmt.parseInt(u32, it.next() orelse "0", 10) catch 0;
    const v: std.SemanticVersion = .{ .major = major, .minor = minor, .patch = 0 };
    return v.order(min_cmake) != .lt;
}

test cmakeVersionOk {
    try std.testing.expect(cmakeVersionOk("cmake version 3.31.6"));
    try std.testing.expect(cmakeVersionOk("cmake version 4.0.0-rc1"));
    try std.testing.expect(cmakeVersionOk("cmake version 3.15.0"));
    try std.testing.expect(!cmakeVersionOk("cmake version 3.10.2"));
    try std.testing.expect(!cmakeVersionOk("something else"));
}
