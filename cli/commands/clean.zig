const std = @import("std");
const Io = std.Io;
const Ctx = @import("../Ctx.zig");
const Args = @import("../Args.zig");
const cmake = @import("../cmake.zig");
const targets = @import("../targets.zig");

pub const usage =
    \\Usage: dino clean [targets...] [-C <dir>]
    \\
    \\Remove build directories created by dino. With no targets, removes all of
    \\them. Directories under build/ that dino did not create are left alone.
    \\
;

pub fn main(ctx: *Ctx, raw: []const []const u8) Ctx.Error!u8 {
    var args: Args = .{ .items = raw };
    var project: []const u8 = ".";
    var names: std.ArrayList([]const u8) = .empty;
    while (args.next()) |arg| {
        if (Args.isHelp(arg)) {
            ctx.print("{s}", .{usage});
            return 0;
        } else if (try args.value(ctx, arg, &.{"-C"})) |v| {
            project = v;
        } else if (std.mem.startsWith(u8, arg, "-")) {
            return ctx.fail("unknown option '{s}'\n\n{s}", .{ arg, usage });
        } else {
            const t = targets.resolve(arg) catch return ctx.fail("unknown target '{s}'", .{arg});
            names.append(ctx.arena, t.name) catch @panic("OOM");
        }
    }

    const build_root = ctx.join(&.{ project, "build" });
    if (names.items.len == 0) {
        var dir = Io.Dir.cwd().openDir(ctx.io, build_root, .{ .iterate = true }) catch {
            ctx.print("nothing to clean\n", .{});
            return 0;
        };
        defer dir.close(ctx.io);
        var it = dir.iterate();
        while (it.next(ctx.io) catch |e| return ctx.fail("reading {s}: {t}", .{ build_root, e })) |entry| {
            if (entry.kind == .directory) names.append(ctx.arena, ctx.arena.dupe(u8, entry.name) catch @panic("OOM")) catch @panic("OOM");
        }
    }

    const cwd = Io.Dir.cwd();
    var removed: usize = 0;
    for (names.items) |name| {
        const dir = ctx.join(&.{ build_root, name });
        // Only directories carrying dino's stamp are ours to delete.
        if (!ctx.exists(ctx.join(&.{ dir, cmake.stamp_file }))) continue;
        cwd.deleteTree(ctx.io, dir) catch |e| return ctx.fail("removing {s}: {t}", .{ dir, e });
        ctx.print("    removed  {s}\n", .{dir});
        removed += 1;
    }
    cwd.deleteDir(ctx.io, build_root) catch {};
    if (removed == 0) ctx.print("nothing to clean\n", .{});
    return 0;
}
