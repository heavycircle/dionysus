//! Minimal argument cursor shared by the subcommands.
const std = @import("std");
const Ctx = @import("Ctx.zig");

const Args = @This();

items: []const []const u8,
index: usize = 0,

pub fn next(a: *Args) ?[]const u8 {
    if (a.index >= a.items.len) return null;
    defer a.index += 1;
    return a.items[a.index];
}

/// Everything after the current position, e.g. what follows `--`.
pub fn rest(a: *Args) []const []const u8 {
    defer a.index = a.items.len;
    return a.items[a.index..];
}

/// Value of a flag given as `--flag value`, `--flag=value`, or `-jN` for short
/// flags. An empty value is a usage error.
pub fn value(a: *Args, ctx: *Ctx, arg: []const u8, comptime names: []const []const u8) Ctx.Error!?[]const u8 {
    inline for (names) |name| {
        if (match(a, arg, name)) |found| {
            if (found.len == 0) return ctx.usage("{s} requires a value", .{name});
            return found;
        }
    }
    return null;
}

/// null: `arg` isn't this flag; "" when its value is missing or empty.
fn match(a: *Args, arg: []const u8, comptime name: []const u8) ?[]const u8 {
    if (std.mem.eql(u8, arg, name)) return a.next() orelse "";
    if (!std.mem.startsWith(u8, arg, name)) return null;
    const tail = arg[name.len..];
    if (tail.len != 0 and tail[0] == '=') return tail[1..];
    return if (name.len == 2) tail else null;
}

pub fn isHelp(arg: []const u8) bool {
    return std.mem.eql(u8, arg, "-h") or std.mem.eql(u8, arg, "--help");
}

test "flag values" {
    var ctx: Ctx = undefined;
    var a: Args = .{ .items = &.{ "-j", "4", "-j8", "--jobs=2", "--jobs", "3", "-C=dir", "--jobsx" } };
    const names: []const []const u8 = &.{ "-j", "--jobs" };
    try std.testing.expectEqualStrings("4", (try a.value(&ctx, a.next().?, names)).?);
    try std.testing.expectEqualStrings("8", (try a.value(&ctx, a.next().?, names)).?);
    try std.testing.expectEqualStrings("2", (try a.value(&ctx, a.next().?, names)).?);
    try std.testing.expectEqualStrings("3", (try a.value(&ctx, a.next().?, names)).?);
    try std.testing.expectEqualStrings("dir", (try a.value(&ctx, a.next().?, &.{"-C"})).?);
    try std.testing.expect((try a.value(&ctx, a.next().?, names)) == null);

    var empty: Args = .{ .items = &.{ "-C", "", "-C=", "--jobs=", "-C" } };
    try std.testing.expectEqualStrings("", match(&empty, empty.next().?, "-C").?);
    try std.testing.expectEqualStrings("", match(&empty, empty.next().?, "-C").?);
    try std.testing.expectEqualStrings("", match(&empty, empty.next().?, "--jobs").?);
    try std.testing.expectEqualStrings("", match(&empty, empty.next().?, "-C").?);
}
