//! Organization libraries listed in DINO_LIBRARY_PATH. cmake/Dino.cmake adds
//! each one to every project, next to dino_core and dino_util.
const std = @import("std");
const Io = std.Io;
const Ctx = @import("Ctx.zig");

pub const env_var = "DINO_LIBRARY_PATH";

pub const Library = struct {
    /// As written in DINO_LIBRARY_PATH.
    entry: []const u8,
    /// Absolute, with symlinks resolved; null if it doesn't exist.
    path: ?[]const u8,
    problem: ?[]const u8 = null,
};

/// Every entry of DINO_LIBRARY_PATH with what's wrong with it, for `dino doctor`.
/// A directory listed twice appears once.
pub fn inspect(ctx: *Ctx) []const Library {
    const value = ctx.env.get(env_var) orelse return &.{};
    var list: std.ArrayList(Library) = .empty;
    var it = entries(value, std.fs.path.delimiter);
    while (it.next()) |entry| {
        const real = Io.Dir.cwd().realPathFileAlloc(ctx.io, entry, ctx.arena) catch null;
        var lib: Library = .{ .entry = entry, .path = real };
        if (real == null) {
            lib.problem = "does not exist";
        } else if (!ctx.exists(ctx.join(&.{ real.?, "CMakeLists.txt" }))) {
            lib.problem = "has no CMakeLists.txt";
        } else if (duplicate(list.items, real.?)) |earlier| {
            if (std.mem.eql(u8, earlier.path.?, real.?)) continue;
            lib.problem = ctx.sprint("has the same directory name as '{s}'", .{earlier.entry});
        }
        list.append(ctx.arena, lib) catch @panic("OOM");
    }
    return list.items;
}

/// The libraries to build against, or an error naming the first bad entry.
pub fn resolve(ctx: *Ctx) Ctx.Error![]const []const u8 {
    var paths: std.ArrayList([]const u8) = .empty;
    for (inspect(ctx)) |lib| {
        if (lib.problem) |p| return ctx.fail("{s} entry '{s}' {s}", .{ env_var, lib.entry, p });
        paths.append(ctx.arena, lib.path.?) catch @panic("OOM");
    }
    return paths.items;
}

/// Empty entries are skipped, as PATH lookups do.
fn entries(value: []const u8, delimiter: u8) std.mem.TokenIterator(u8, .scalar) {
    return std.mem.tokenizeScalar(u8, value, delimiter);
}

/// Each library builds in _dino_libs/<directory name>, so names must be unique.
fn duplicate(earlier: []const Library, path: []const u8) ?Library {
    const name = std.fs.path.basename(path);
    for (earlier) |lib| {
        const other = lib.path orelse continue;
        if (lib.problem == null and std.mem.eql(u8, std.fs.path.basename(other), name)) return lib;
    }
    return null;
}

test "entries skip empty items" {
    var it = entries(":/opt/a::b:", ':');
    try std.testing.expectEqualStrings("/opt/a", it.next().?);
    try std.testing.expectEqualStrings("b", it.next().?);
    try std.testing.expect(it.next() == null);

    var win = entries("C:\\org\\a;;D:\\b", ';');
    try std.testing.expectEqualStrings("C:\\org\\a", win.next().?);
    try std.testing.expectEqualStrings("D:\\b", win.next().?);
    try std.testing.expect(win.next() == null);
}

test "duplicate matches directory names" {
    const seen = [_]Library{
        .{ .entry = "a", .path = "/opt/org/logging" },
        .{ .entry = "b", .path = "/opt/gone", .problem = "does not exist" },
    };
    try std.testing.expectEqualStrings("a", duplicate(&seen, "/srv/logging").?.entry);
    try std.testing.expect(duplicate(&seen, "/opt/org/config") == null);
    try std.testing.expect(duplicate(&seen, "/elsewhere/gone") == null);
}
