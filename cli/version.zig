//! build.zig.zon always names the *next* release (`0.2.0-dev`); only the
//! release commit itself drops the `-dev`. Builds between releases append how
//! far they are from the last tag, so no untagged build can pass for a release.
//! Imported by build.zig, so this file may depend on std alone.
const std = @import("std");

pub const Error = error{
    /// HEAD is exactly on a tag that build.zig.zon disagrees with.
    TagMismatch,
    /// Commits exist after a tag but build.zig.zon was not bumped past it.
    NotBumped,
    UnexpectedDescribe,
} || std.mem.Allocator.Error;

/// `describe` is the output of
/// `git describe --tags --always --abbrev=9 --match "v[0-9]*.[0-9]*.[0-9]*"`.
pub fn fromDescribe(arena: std.mem.Allocator, base: []const u8, describe: []const u8) Error![]const u8 {
    const base_ver = std.SemanticVersion.parse(base) catch return error.UnexpectedDescribe;
    const text = std.mem.trim(u8, describe, " \r\n");

    // --always prints a bare abbreviated hash when no release tag is reachable.
    if (!std.mem.startsWith(u8, text, "v")) {
        if (text.len == 0 or !isHex(text)) return error.UnexpectedDescribe;
        return std.fmt.allocPrint(arena, "{s}+g{s}", .{ base, text });
    }

    var it = std.mem.splitScalar(u8, text[1..], '-');
    const tag = it.first();
    const height = it.next() orelse {
        if (!std.mem.eql(u8, tag, base)) return error.TagMismatch;
        return base;
    };
    const commit = it.next() orelse return error.UnexpectedDescribe;
    if (it.next() != null) return error.UnexpectedDescribe;
    if (commit.len < 2 or commit[0] != 'g' or !isHex(commit[1..])) return error.UnexpectedDescribe;
    const tag_ver = std.SemanticVersion.parse(tag) catch return error.UnexpectedDescribe;

    const core: std.SemanticVersion = .{ .major = base_ver.major, .minor = base_ver.minor, .patch = base_ver.patch };
    if (base_ver.pre == null or core.order(tag_ver) != .gt) return error.NotBumped;
    return std.fmt.allocPrint(arena, "{s}.{s}+{s}", .{ base, height, commit });
}

fn isHex(s: []const u8) bool {
    for (s) |c| if (!std.ascii.isHex(c)) return false;
    return true;
}

test fromDescribe {
    var arena_state = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena_state.deinit();
    const arena = arena_state.allocator();
    const expectVersion = struct {
        fn f(a: std.mem.Allocator, expected: []const u8, base: []const u8, describe: []const u8) !void {
            try std.testing.expectEqualStrings(expected, try fromDescribe(a, base, describe));
        }
    }.f;

    try expectVersion(arena, "0.1.0", "0.1.0", "v0.1.0\n");
    try expectVersion(arena, "0.2.0-dev.14+g1a2b3c4d5", "0.2.0-dev", "v0.1.0-14-g1a2b3c4d5");
    try expectVersion(arena, "0.1.1-dev.3+g1a2b3c4d5", "0.1.1-dev", "v0.1.0-3-g1a2b3c4d5");
    try expectVersion(arena, "0.1.0-dev+g1a2b3c4d5", "0.1.0-dev", "1a2b3c4d5");

    const expectError = std.testing.expectError;
    try expectError(error.TagMismatch, fromDescribe(arena, "0.2.0-dev", "v0.1.0"));
    try expectError(error.NotBumped, fromDescribe(arena, "0.1.0-dev", "v0.1.0-2-g1a2b3c4d5"));
    try expectError(error.NotBumped, fromDescribe(arena, "0.1.0", "v0.1.0-2-g1a2b3c4d5"));
    try expectError(error.NotBumped, fromDescribe(arena, "0.2.0", "v0.1.0-2-g1a2b3c4d5"));
    try expectError(error.UnexpectedDescribe, fromDescribe(arena, "0.2.0-dev", "v0.1.0-2-1a2b3c4d5"));
    try expectError(error.UnexpectedDescribe, fromDescribe(arena, "0.2.0-dev", "fatal: not a git repository"));
}
