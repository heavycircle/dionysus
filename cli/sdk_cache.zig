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
    if (overridden(ctx)) return checkOverride(ctx, ctx.env.get("DINO_SDK_PATH").?);
    const dir = ctx.join(&.{ try home(ctx), "sdk", ctx.sprint("{s}-{s}", .{ build_options.sdk_version, &contentHash() }) });
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

/// DINO_SDK_PATH names a working tree, which is expected to change.
pub fn overridden(ctx: *Ctx) bool {
    return nonEmpty(ctx.env.get("DINO_SDK_PATH")) != null;
}

/// What differs between the unpacked SDK at `dir` and the one inside this
/// dino, for `dino doctor`; null when nothing does.
pub fn verify(ctx: *Ctx, dir: []const u8) ?[]const u8 {
    var changed: usize = 0;
    var first: []const u8 = "";
    for (embedded.files) |file| {
        const on_disk = Io.Dir.cwd().readFileAlloc(ctx.io, ctx.join(&.{ dir, file.path }), ctx.arena, .limited(file.data.len + 1)) catch "";
        if (std.mem.eql(u8, on_disk, file.data)) continue;
        if (changed == 0) first = file.path;
        changed += 1;
    }
    if (changed == 0) return null;
    const which = if (changed == 1) first else ctx.sprint("{s} and {d} more", .{ first, changed - 1 });
    return ctx.sprint("{s} differs from this dino's SDK ({s}); remove the directory and the next command unpacks it again", .{ dir, which });
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

/// The unpacked files are read-only, so an edit made by following an editor's
/// go-to-definition into the SDK can't change every project's build. Windows
/// keeps them writable: a read-only file can't be deleted there.
const file_permissions: Io.File.Permissions = if (builtin.os.tag == .windows) .default_file else .fromMode(0o444);

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
        cwd.writeFile(ctx.io, .{ .sub_path = dest, .data = file.data, .flags = .{ .permissions = file_permissions } }) catch |e|
            return ctx.fail("writing {s}: {t}", .{ dest, e });
    }
    cwd.writeFile(ctx.io, .{ .sub_path = ctx.join(&.{ tmp, marker }), .data = build_options.sdk_version }) catch |e|
        return ctx.fail("writing SDK marker in {s}: {t}", .{ tmp, e });
    removeLeftovers(ctx, dir, tmp);

    if (ctx.exists(ctx.join(&.{ dir, marker }))) return;
    // Never delete a complete SDK: another dino may have renamed its own into
    // place since the check above. The marker is written before the rename,
    // so a directory without one is never that; it is left from an older dino
    // or an interrupted cleanup, and moved aside.
    if (ctx.exists(dir)) {
        const stale = ctx.sprint("{s}.stale-{x}", .{ dir, &suffix });
        Io.Dir.rename(cwd, dir, cwd, stale, ctx.io) catch {};
        cwd.deleteTree(ctx.io, stale) catch {};
    }
    Io.Dir.rename(cwd, tmp, cwd, dir, ctx.io) catch |e| {
        if (ctx.exists(ctx.join(&.{ dir, marker }))) return;
        return ctx.fail("installing SDK to {s}: {t}", .{ dir, e });
    };
}

/// Temporaries of unpacks that were killed (Ctrl-C on a first run) and stale
/// directories whose removal failed. Only ones older than ten minutes, measured
/// against `tmp` just written, so a concurrent unpack's are left alone.
fn removeLeftovers(ctx: *Ctx, dir: []const u8, tmp: []const u8) void {
    const cwd = Io.Dir.cwd();
    const now = (cwd.statFile(ctx.io, ctx.join(&.{ tmp, marker }), .{}) catch return).mtime.nanoseconds;
    const parent_path = std.fs.path.dirname(dir) orelse return;
    var parent = cwd.openDir(ctx.io, parent_path, .{ .iterate = true }) catch return;
    defer parent.close(ctx.io);
    var it = parent.iterate();
    while (it.next(ctx.io) catch return) |entry| {
        if (entry.kind != .directory) continue;
        if (std.mem.indexOf(u8, entry.name, ".tmp-") == null and std.mem.indexOf(u8, entry.name, ".stale-") == null) continue;
        const stat = parent.statFile(ctx.io, entry.name, .{}) catch continue;
        if (now - stat.mtime.nanoseconds < 10 * std.time.ns_per_min) continue;
        parent.deleteTree(ctx.io, entry.name) catch {};
    }
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

test "sdk CMake version matches build.zig.zon" {
    const cmake = for (embedded.files) |file| {
        if (std.mem.eql(u8, file.path, "CMakeLists.txt")) break file.data;
    } else return error.MissingCMakeLists;
    const number = try between(cmake, "project(DinoSDK VERSION ", " ");
    const pre = try between(cmake, "set(DINO_VERSION_PRERELEASE \"", "\")");
    const expected = try std.fmt.allocPrint(std.testing.allocator, "{s}{s}", .{ number, pre });
    defer std.testing.allocator.free(expected);
    try std.testing.expectEqualStrings(build_options.sdk_version, expected);
}

fn between(haystack: []const u8, start: []const u8, end: []const u8) ![]const u8 {
    const i = (std.mem.indexOf(u8, haystack, start) orelse return error.NotFound) + start.len;
    const len = std.mem.indexOf(u8, haystack[i..], end) orelse return error.NotFound;
    return haystack[i..][0..len];
}

test "content hash is stable hex" {
    const a = contentHash();
    const b = contentHash();
    try std.testing.expectEqualSlices(u8, &a, &b);
    for (a) |c| try std.testing.expect(std.ascii.isHex(c));
}
