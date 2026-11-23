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

    // Parsed from the right: a tag can have `-` of its own (v1.0.0-rc1), and
    // describe only appends `-<height>-g<hash>` after it.
    const rest = text[1..];
    const after = afterTag(rest) orelse {
        if (looksDescribed(rest)) return error.UnexpectedDescribe;
        if (!std.mem.eql(u8, rest, base)) return error.TagMismatch;
        return base;
    };
    const tag_ver = std.SemanticVersion.parse(after.tag) catch return error.UnexpectedDescribe;

    // A -dev (or later prerelease) version newer than the tag, prereleases
    // included: after v1.0.0-rc1, 1.0.0-rc2 is newer and 1.0.0-dev isn't.
    if (base_ver.pre == null or base_ver.order(tag_ver) != .gt) return error.NotBumped;
    return std.fmt.allocPrint(arena, "{s}.{s}+{s}", .{ base, after.height, after.commit });
}

/// `-<height>-<something>` at the end that afterTag didn't take: a describe
/// output gone wrong, not a tag (whose prerelease parts aren't all digits).
fn looksDescribed(text: []const u8) bool {
    const dash2 = std.mem.lastIndexOfScalar(u8, text, '-') orelse return false;
    const dash1 = std.mem.lastIndexOfScalar(u8, text[0..dash2], '-') orelse return false;
    const height = text[dash1 + 1 .. dash2];
    if (height.len == 0) return false;
    for (height) |c| if (!std.ascii.isDigit(c)) return false;
    return true;
}

const AfterTag = struct { tag: []const u8, height: []const u8, commit: []const u8 };

/// `<tag>-<height>-g<hash>` split up, or null when `text` is a tag alone.
fn afterTag(text: []const u8) ?AfterTag {
    const dash2 = std.mem.lastIndexOfScalar(u8, text, '-') orelse return null;
    const commit = text[dash2 + 1 ..];
    if (commit.len < 2 or commit[0] != 'g' or !isHex(commit[1..])) return null;
    const dash1 = std.mem.lastIndexOfScalar(u8, text[0..dash2], '-') orelse return null;
    const height = text[dash1 + 1 .. dash2];
    if (height.len == 0) return null;
    for (height) |c| if (!std.ascii.isDigit(c)) return null;
    return .{ .tag = text[0..dash1], .height = height, .commit = commit };
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
    try expectVersion(arena, "1.0.0-rc1", "1.0.0-rc1", "v1.0.0-rc1");
    try expectVersion(arena, "1.0.0-rc2.3+g1a2b3c4d5", "1.0.0-rc2", "v1.0.0-rc1-3-g1a2b3c4d5");
    try expectVersion(arena, "1.1.0-dev.3+g1a2b3c4d5", "1.1.0-dev", "v1.0.0-rc1-3-g1a2b3c4d5");

    const expectError = std.testing.expectError;
    try expectError(error.TagMismatch, fromDescribe(arena, "0.2.0-dev", "v0.1.0"));
    try expectError(error.NotBumped, fromDescribe(arena, "0.1.0-dev", "v0.1.0-2-g1a2b3c4d5"));
    try expectError(error.NotBumped, fromDescribe(arena, "0.1.0", "v0.1.0-2-g1a2b3c4d5"));
    try expectError(error.NotBumped, fromDescribe(arena, "0.2.0", "v0.1.0-2-g1a2b3c4d5"));
    try expectError(error.NotBumped, fromDescribe(arena, "1.0.0-dev", "v1.0.0-rc1-3-g1a2b3c4d5"));
    try expectError(error.TagMismatch, fromDescribe(arena, "1.0.0", "v1.0.0-rc1"));
    try expectError(error.UnexpectedDescribe, fromDescribe(arena, "0.2.0-dev", "v0.1.0-2-1a2b3c4d5"));
    try expectError(error.UnexpectedDescribe, fromDescribe(arena, "0.2.0-dev", "fatal: not a git repository"));
}
