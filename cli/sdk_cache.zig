//! The SDK ships inside the dino binary and is unpacked once per version into a
//! shared cache, so projects never need their own copy.
const std = @import("std");
const builtin = @import("builtin");
const Io = std.Io;
const build_options = @import("build_options");
const embedded = @import("sdk");
const Ctx = @import("Ctx.zig");

const marker = ".dino-sdk";

/// Absolute path of the SDK to build against.
pub fn path(ctx: *Ctx) Ctx.Error![]const u8 {
    if (ctx.env.get("DINO_SDK_PATH")) |override| {
        if (override.len != 0) return checkOverride(ctx, override);
    }
    const dir = ctx.join(&.{ try home(ctx), "sdk", ctx.sprint("{s}-{s}", .{ build_options.version, &contentHash() }) });
    if (!ctx.exists(ctx.join(&.{ dir, marker }))) try unpack(ctx, dir);
    // CMake resolves a relative CMAKE_TOOLCHAIN_FILE against the build directory.
    return Io.Dir.cwd().realPathFileAlloc(ctx.io, dir, ctx.arena) catch |e|
        ctx.fail("resolving SDK path {s}: {t}", .{ dir, e });
}

/// Root of dino's per-user state: $DINO_HOME, else the platform cache directory.
pub fn home(ctx: *Ctx) Ctx.Error![]const u8 {
    if (nonEmpty(ctx.env.get("DINO_HOME"))) |dir| return dir;
    const base = switch (builtin.os.tag) {
        .windows => nonEmpty(ctx.env.get("LOCALAPPDATA")),
        .macos => if (nonEmpty(ctx.env.get("HOME"))) |h| ctx.join(&.{ h, "Library", "Caches" }) else null,
        else => nonEmpty(ctx.env.get("XDG_CACHE_HOME")) orelse
            if (nonEmpty(ctx.env.get("HOME"))) |h| ctx.join(&.{ h, ".cache" }) else null,
    } orelse return ctx.fail("cannot locate a cache directory; set DINO_HOME", .{});
    return ctx.join(&.{ base, "dino" });
}

fn nonEmpty(value: ?[]const u8) ?[]const u8 {
    const v = value orelse return null;
    return if (v.len == 0) null else v;
}

fn checkOverride(ctx: *Ctx, override: []const u8) Ctx.Error![]const u8 {
    const abs = Io.Dir.cwd().realPathFileAlloc(ctx.io, override, ctx.arena) catch
        return ctx.fail("DINO_SDK_PATH '{s}' does not exist", .{override});
    if (!ctx.exists(ctx.join(&.{ abs, "cmake", "toolchain.cmake" })))
        return ctx.fail("DINO_SDK_PATH '{s}' is not a dino SDK (missing cmake/toolchain.cmake)", .{override});
    return abs;
}

/// Hashing the content (not just the version) keeps development builds of dino
/// from reusing a stale SDK when the version number hasn't been bumped.
pub fn contentHash() [16]u8 {
    var hasher = std.hash.Wyhash.init(0);
    for (embedded.files) |file| {
        hasher.update(file.path);
        hasher.update(&.{0});
        hasher.update(file.data);
    }
    var out: [16]u8 = undefined;
    _ = std.fmt.bufPrint(&out, "{x:0>16}", .{hasher.final()}) catch unreachable;
    return out;
}

/// Unpacks into a temporary sibling and renames it into place, so a concurrent
/// or interrupted unpack never leaves a half-written SDK behind.
fn unpack(ctx: *Ctx, dir: []const u8) Ctx.Error!void {
    const cwd = Io.Dir.cwd();
    var suffix: [8]u8 = undefined;
    ctx.io.random(&suffix);
    const tmp = ctx.sprint("{s}.tmp-{x}", .{ dir, &suffix });
    defer cwd.deleteTree(ctx.io, tmp) catch {};

    for (embedded.files) |file| {
        const dest = ctx.join(&.{ tmp, file.path });
        if (std.fs.path.dirname(dest)) |parent| {
            cwd.createDirPath(ctx.io, parent) catch |e| return ctx.fail("creating {s}: {t}", .{ parent, e });
        }
        cwd.writeFile(ctx.io, .{ .sub_path = dest, .data = file.data }) catch |e|
            return ctx.fail("writing {s}: {t}", .{ dest, e });
    }
    cwd.writeFile(ctx.io, .{ .sub_path = ctx.join(&.{ tmp, marker }), .data = build_options.version }) catch |e|
        return ctx.fail("writing SDK marker in {s}: {t}", .{ tmp, e });

    if (ctx.exists(ctx.join(&.{ dir, marker }))) return;
    // A directory without the marker is a leftover from a crashed older dino.
    if (ctx.exists(dir)) cwd.deleteTree(ctx.io, dir) catch {};
    Io.Dir.rename(cwd, tmp, cwd, dir, ctx.io) catch |e| {
        if (ctx.exists(ctx.join(&.{ dir, marker }))) return;
        return ctx.fail("installing SDK to {s}: {t}", .{ dir, e });
    };
}

test "embedded sdk contents" {
    var has_cmake = false;
    var has_toolchain = false;
    var has_module = false;
    for (embedded.files) |file| {
        try std.testing.expect(file.data.len > 0);
        try std.testing.expect(!std.mem.startsWith(u8, file.path, "scripts/"));
        if (std.mem.eql(u8, file.path, "CMakeLists.txt")) has_cmake = true;
        if (std.mem.eql(u8, file.path, "cmake/toolchain.cmake")) has_toolchain = true;
        if (std.mem.eql(u8, file.path, "cmake/Dino.cmake")) has_module = true;
    }
    try std.testing.expect(has_cmake and has_toolchain and has_module);
}

test "content hash is stable hex" {
    const a = contentHash();
    const b = contentHash();
    try std.testing.expectEqualSlices(u8, &a, &b);
    for (a) |c| try std.testing.expect(std.ascii.isHex(c));
}
