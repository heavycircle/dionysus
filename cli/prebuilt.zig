//! With DINO_SDK_PREBUILT=1, the SDK is built on its own once per zig version,
//! target and build type, into the unpacked SDK's lib/ directory, and every
//! project imports it (sdk/cmake/prebuilt.cmake) instead of compiling it again.
//! Hermetic: the user's flags never reach it, which is why the import checks
//! the project's flags and keeps the source build for any that could matter.
const std = @import("std");
const Io = std.Io;
const Ctx = @import("Ctx.zig");
const cmake = @import("cmake.zig");
const targets = @import("targets.zig");
const Target = targets.Target;

const marker = "dino-prebuilt.cmake";

pub fn enabled(ctx: *Ctx) bool {
    const value = ctx.env.get("DINO_SDK_PREBUILT") orelse return false;
    return std.mem.eql(u8, value, "1");
}

/// For `dino doctor`.
pub fn describe(ctx: *Ctx, sdk: []const u8) []const u8 {
    if (!enabled(ctx)) return "off (DINO_SDK_PREBUILT=1 builds the SDK once per target, for every project)";
    if (ctx.env.get("DINO_SDK_PATH")) |p| if (p.len != 0) return "on, but not used with DINO_SDK_PATH";
    return ctx.sprint("on, built into {s}", .{ctx.join(&.{ sdk, "lib" })});
}

/// The prebuilt SDK for this target and mode, built first if needed; "" for
/// the source build: not asked for, DINO_SDK_PATH (a working tree changes
/// without its version changing), the dynamic --memcheck build, or when it
/// couldn't be built, which is a warning rather than a failure.
pub fn dir(ctx: *Ctx, sdk: []const u8, zig: []const u8, target: Target, mode: cmake.Mode) Ctx.Error![]const u8 {
    if (!enabled(ctx) or zig.len == 0) return "";
    if (ctx.env.get("DINO_SDK_PATH")) |p| if (p.len != 0) return "";
    if (std.mem.eql(u8, target.triple, targets.memcheck.triple)) return "";

    const name = ctx.sprint("{s}-{s}", .{ target.triple, @tagName(mode) });
    const out = ctx.join(&.{ sdk, "lib", ctx.sprint("zig-{s}", .{safe(ctx, zig)}), name });
    if (ctx.exists(ctx.join(&.{ out, marker }))) return out;
    // Never deleted: it may be another dino's set, just renamed into place.
    if (ctx.exists(out)) {
        _ = skip(ctx, "{s} is not a complete prebuilt SDK; remove it to build it again", .{out});
        return "";
    }
    return if (try build(ctx, sdk, target, mode, out)) out else "";
}

/// A zig version as one path component.
fn safe(ctx: *Ctx, version: []const u8) []const u8 {
    const copy = ctx.arena.dupe(u8, version) catch @panic("OOM");
    for (copy) |*c| {
        if (!std.ascii.isAlphanumeric(c.*) and std.mem.indexOfScalar(u8, ".+-_", c.*) == null) c.* = '_';
    }
    return copy;
}

/// Configures and builds the SDK as its own project in a temporary sibling of
/// `out`, then renames the result into place, as the SDK itself is unpacked:
/// a concurrent or interrupted build never leaves a half-written set behind.
fn build(ctx: *Ctx, sdk: []const u8, target: Target, mode: cmake.Mode, out: []const u8) Ctx.Error!bool {
    const cwd = Io.Dir.cwd();
    var suffix: [8]u8 = undefined;
    ctx.io.random(&suffix);
    const tmp = ctx.sprint("{s}.tmp-{x}", .{ out, &suffix });
    defer cwd.deleteTree(ctx.io, tmp) catch {};
    const tree = ctx.join(&.{ tmp, "build" });
    const result = ctx.join(&.{ tmp, "out" });
    cwd.createDirPath(ctx.io, tmp) catch |e| return skip(ctx, "creating {s}: {t}", .{ tmp, e });

    // None of the user's flags: the set is shared by every project.
    const env = ctx.arena.create(std.process.Environ.Map) catch @panic("OOM");
    env.* = ctx.env.clone(ctx.arena) catch @panic("OOM");
    for ([_][]const u8{ "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS" }) |v| _ = env.swapRemove(v);

    ctx.print("    building the SDK for {s} ({t}) once, for every project\n", .{ target.triple, mode });
    var configure: std.ArrayList([]const u8) = .empty;
    configure.appendSlice(ctx.arena, &.{ "cmake", "-S", sdk, "-B", tree, "--log-level=WARNING" }) catch @panic("OOM");
    if (try cmake.generator(ctx)) |g| configure.appendSlice(ctx.arena, &.{ "-G", g }) catch @panic("OOM");
    configure.appendSlice(ctx.arena, &.{
        ctx.sprint("-DCMAKE_TOOLCHAIN_FILE={s}/cmake/toolchain.cmake", .{cmake.cmakePath(ctx, sdk)}),
        ctx.sprint("-DZIG_TARGET={s}", .{target.triple}),
        ctx.sprint("-DCMAKE_BUILD_TYPE={s}", .{mode.cmakeName()}),
        ctx.sprint("-DDINO_SDK_PREBUILD={s}", .{cmake.cmakePath(ctx, result)}),
    }) catch @panic("OOM");
    if (!try quiet(ctx, configure.items, env)) return false;
    if (!try quiet(ctx, &.{ "cmake", "--build", tree }, env)) return false;
    if (!ctx.exists(ctx.join(&.{ result, marker }))) return skip(ctx, "the SDK build left no {s} in {s}", .{ marker, result });

    if (ctx.exists(ctx.join(&.{ out, marker }))) return true;
    Io.Dir.rename(cwd, result, cwd, out, ctx.io) catch |e| {
        // Another dino renamed its set into place first.
        if (ctx.exists(ctx.join(&.{ out, marker }))) return true;
        return skip(ctx, "installing the prebuilt SDK to {s}: {t}", .{ out, e });
    };
    return true;
}

/// Runs a step of the SDK build, showing its output only if it fails.
fn quiet(ctx: *Ctx, argv: []const []const u8, env: *const std.process.Environ.Map) Ctx.Error!bool {
    const run = std.process.run(ctx.arena, ctx.io, .{
        .argv = argv,
        .environ_map = env,
        .stdout_limit = .limited(1 << 20),
        .stderr_limit = .limited(1 << 20),
    }) catch |e| return skip(ctx, "unable to run {s}: {t}", .{ argv[0], e });
    if (Ctx.exitCode(run.term) == 0) return true;
    ctx.err.print("{s}{s}", .{ run.stdout, run.stderr }) catch {};
    return skip(ctx, "building the SDK failed", .{});
}

fn skip(ctx: *Ctx, comptime fmt: []const u8, args: anytype) bool {
    ctx.warn(fmt ++ "; compiling the SDK in the project instead", args);
    return false;
}
