const std = @import("std");
const builtin = @import("builtin");
const Ctx = @import("../Ctx.zig");
const Args = @import("../Args.zig");
const sdk_cache = @import("../sdk_cache.zig");
const prebuilt = @import("../prebuilt.zig");
const libraries = @import("../libraries.zig");
const targets = @import("../targets.zig");

pub const usage =
    \\Usage: dino doctor
    \\
    \\Check that the tools dino needs are installed and that every library
    \\in DINO_LIBRARY_PATH can be used. Exits non-zero if a required tool is
    \\missing, doesn't run or has the wrong version, or a library entry is
    \\broken. Emulators and valgrind are optional: `dino run` and `dino test`
    \\need the first for foreign targets, `dino test --memcheck` the second.
    \\
;

const min_cmake: std.SemanticVersion = .{ .major = 3, .minor = 20, .patch = 0 };
/// The Zig that built dino is the one it is developed and tested with
/// (minimum_zig_version in build.zig.zon).
const tested_zig = builtin.zig_version;

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
        if (versionOf(ctx, &.{ zig, "version" })) |version| {
            const line = firstLine(version);
            const ok = zigVersionOk(line);
            report(ctx, ok, "zig", if (ok)
                ctx.sprint("{s} ({s})", .{ line, zig })
            else
                ctx.sprint("{s} ({s}); dino needs Zig {d}.{d}", .{ line, zig, tested_zig.major, tested_zig.minor }));
            if (!ok) problems += 1;
        } else {
            report(ctx, false, "zig", ctx.sprint("{s} is there, but `zig version` failed", .{zig}));
            problems += 1;
        }
    } else {
        report(ctx, false, "zig", "not found; download from https://ziglang.org/download/");
        problems += 1;
    }
    if (ctx.findProgram("cmake")) |cmake| {
        if (versionOf(ctx, &.{ cmake, "--version" })) |version| {
            const line = firstLine(version);
            const ok = cmakeVersionOk(line);
            report(ctx, ok, "cmake", if (ok) ctx.sprint("{s} ({s})", .{ line, cmake }) else ctx.sprint("{s}; {d}.{d} or newer is required", .{ line, min_cmake.major, min_cmake.minor }));
            if (!ok) problems += 1;
        } else {
            report(ctx, false, "cmake", ctx.sprint("{s} is there, but `cmake --version` failed", .{cmake}));
            problems += 1;
        }
    } else {
        report(ctx, false, "cmake", ctx.sprint("not found; install CMake {d}.{d} or newer", .{ min_cmake.major, min_cmake.minor }));
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
        if (sdk_cache.overridden(ctx)) {
            report(ctx, true, "sdk", sdk);
        } else if (sdk_cache.verify(ctx, sdk)) |problem| {
            report(ctx, false, "sdk", problem);
            problems += 1;
        } else {
            report(ctx, true, "sdk", sdk);
        }
        report(ctx, true, "prebuilt", prebuilt.describe(ctx, sdk));
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

    ctx.print("\nRunners for `dino run` and `dino test` (optional):\n", .{});
    for (targets.known) |t| {
        switch (targets.runnerFor(t)) {
            .native => report(ctx, true, t.name, "native"),
            .qemu => |name| if (ctx.findProgram(ctx.sprint("{s}-static", .{name})) orelse ctx.findProgram(name)) |p|
                report(ctx, true, t.name, p)
            else
                report(ctx, false, t.name, ctx.sprint("{s} not found (qemu-user)", .{name})),
            // Whether this wine can run 32-bit programs can't be told from
            // its files (a separate wine32, or a WoW64 build), so say that it
            // matters rather than call win32 fine.
            .wine => if (ctx.findProgram("wine")) |p|
                report(ctx, true, t.name, if (t.arch == .x86) ctx.sprint("{s} (needs its 32-bit support, which isn't checked)", .{p}) else p)
            else
                report(ctx, false, t.name, "wine not found"),
            .unsupported => report(ctx, false, t.name, "cannot run on this host"),
        }
    }

    ctx.print("\nFor `dino test --memcheck` (optional):\n", .{});
    if (targets.runnerFor(targets.memcheck) != .native) {
        report(ctx, false, "valgrind", "needs an x86_64 or aarch64 Linux host");
    } else if (ctx.findProgram("valgrind")) |p| {
        report(ctx, true, "valgrind", p);
    } else {
        report(ctx, false, "valgrind", "not found");
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

/// What the tool prints for its version, or null when it can't be started or
/// fails: a tool that is there but doesn't run is a finding like any other,
/// and the rest of the report still matters.
fn versionOf(ctx: *Ctx, argv: []const []const u8) ?[]const u8 {
    const result = std.process.run(ctx.arena, ctx.io, .{
        .argv = argv,
        .stdout_limit = .limited(1 << 20),
        .stderr_limit = .limited(1 << 20),
    }) catch return null;
    if (Ctx.exitCode(result.term) != 0) return null;
    return std.mem.trim(u8, result.stdout, " \r\n");
}

fn firstLine(text: []const u8) []const u8 {
    return text[0 .. std.mem.indexOfScalar(u8, text, '\n') orelse text.len];
}

/// `zig version` prints "0.17.1", or "0.18.0-dev.123+abcdef" for a development
/// build. Zig changes its language and build system between minor versions,
/// so another minor version is not expected to work; a patch release is.
fn zigVersionOk(line: []const u8) bool {
    const v = std.SemanticVersion.parse(line) catch return false;
    return v.major == tested_zig.major and v.minor == tested_zig.minor and v.pre == null;
}

test zigVersionOk {
    var buffer: [32]u8 = undefined;
    const same = try std.fmt.bufPrint(&buffer, "{d}.{d}.{d}", .{ tested_zig.major, tested_zig.minor, tested_zig.patch });
    try std.testing.expect(zigVersionOk(same));
    const patched = try std.fmt.bufPrint(&buffer, "{d}.{d}.{d}", .{ tested_zig.major, tested_zig.minor, tested_zig.patch + 1 });
    try std.testing.expect(zigVersionOk(patched));
    const next = try std.fmt.bufPrint(&buffer, "{d}.{d}.0", .{ tested_zig.major, tested_zig.minor + 1 });
    try std.testing.expect(!zigVersionOk(next));
    const dev = try std.fmt.bufPrint(&buffer, "{d}.{d}.0-dev.12+abcdef123", .{ tested_zig.major, tested_zig.minor });
    try std.testing.expect(!zigVersionOk(dev));
    try std.testing.expect(!zigVersionOk(""));
    try std.testing.expect(!zigVersionOk("zig: command failed"));
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
    try std.testing.expect(cmakeVersionOk("cmake version 3.20.0"));
    try std.testing.expect(!cmakeVersionOk("cmake version 3.19.8"));
    try std.testing.expect(!cmakeVersionOk("cmake version 3.10.2"));
    try std.testing.expect(!cmakeVersionOk("something else"));
}
