const std = @import("std");
const Io = std.Io;
const Ctx = @import("../Ctx.zig");
const Args = @import("../Args.zig");
const build = @import("build.zig");
const targets = @import("../targets.zig");

pub const usage =
    \\Usage: dino init [directory] [options]
    \\
    \\Create a new dino project in <directory> (default: the current directory).
    \\Existing files are never overwritten.
    \\
    \\Creates:
    \\    CMakeLists.txt         Builds every .c file under src/ and links the SDK
    \\    src/main.c             A starting point that uses the dino SDK
    \\    .gitignore             Ignores build output
    \\    compile_commands.json  For clangd and other editors; init configures the
    \\                           project for this machine so it exists before the
    \\                           first build
    \\
    \\Options:
    \\    --no-configure      Only write the files; don't run CMake
    \\    -h, --help          Show this message
    \\
;

const cmake_template =
    \\cmake_minimum_required(VERSION 3.15)
    \\project({0s} C)
    \\
    \\file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.c)
    \\
    \\# dino_executable() is add_executable() plus linking the dino SDK (dino_core).
    \\dino_executable({0s} ${{SOURCES}})
    \\target_include_directories({0s} PRIVATE src)
    \\
;

const main_template =
    \\#include <stdio.h>
    \\
    \\#include <dino/core/network.h>
    \\
    \\int main(void)
    \\{{
    \\    if (dino_net_init() != 0)
    \\    {{
    \\        fprintf(stderr, "network init failed\n");
    \\        return 1;
    \\    }}
    \\
    \\    puts("Hello from {0s}!");
    \\
    \\    dino_net_cleanup();
    \\    return 0;
    \\}}
    \\
;

const gitignore_template =
    \\build/
    \\compile_commands.json
    \\
;

pub fn main(ctx: *Ctx, raw: []const []const u8) Ctx.Error!u8 {
    var args: Args = .{ .items = raw };
    var dir: ?[]const u8 = null;
    var configure = true;
    while (args.next()) |arg| {
        if (Args.isHelp(arg)) {
            ctx.print("{s}", .{usage});
            return 0;
        } else if (std.mem.eql(u8, arg, "--no-configure")) {
            configure = false;
        } else if (std.mem.startsWith(u8, arg, "-")) {
            return ctx.fail("unknown option '{s}'\n\n{s}", .{ arg, usage });
        } else if (dir != null) {
            return ctx.fail("`dino init` takes a single directory", .{});
        } else {
            dir = arg;
        }
    }
    const root = dir orelse ".";
    const cwd = Io.Dir.cwd();
    cwd.createDirPath(ctx.io, ctx.join(&.{ root, "src" })) catch |e|
        return ctx.fail("creating {s}: {t}", .{ root, e });
    const abs = cwd.realPathFileAlloc(ctx.io, root, ctx.arena) catch |e|
        return ctx.fail("resolving {s}: {t}", .{ root, e });
    const name = projectName(ctx.arena, std.fs.path.basename(abs));

    const files = [_]struct { path: []const u8, data: []const u8 }{
        .{ .path = "CMakeLists.txt", .data = ctx.sprint(cmake_template, .{name}) },
        .{ .path = "src/main.c", .data = ctx.sprint(main_template, .{name}) },
        .{ .path = ".gitignore", .data = gitignore_template },
    };
    for (files) |file| {
        const dest = ctx.join(&.{ root, file.path });
        if (ctx.exists(dest)) {
            ctx.print("    exists   {s}\n", .{dest});
            continue;
        }
        cwd.writeFile(ctx.io, .{ .sub_path = dest, .data = file.data, .flags = .{ .exclusive = true } }) catch |e|
            return ctx.fail("writing {s}: {t}", .{ dest, e });
        ctx.print("    created  {s}\n", .{dest});
    }

    const status: u8 = if (configure) try configureHost(ctx, root) else 0;

    ctx.print("\nNext:\n", .{});
    if (dir) |d| ctx.print("    cd {s}\n", .{d});
    ctx.print("    dino build        # build for this machine\n", .{});
    ctx.print("    dino run          # build and run\n", .{});
    ctx.print("    dino build --all  # build for every target\n", .{});
    return status;
}

/// A missing toolchain only skips this step: the files are still a valid
/// project, and `dino build` reports the missing tool when it's needed.
fn configureHost(ctx: *Ctx, root: []const u8) Ctx.Error!u8 {
    for (build.required_tools) |tool| {
        if (ctx.findProgram(tool) == null) {
            ctx.warn("{s} not found, skipping configure; compile_commands.json appears after the first `dino build`", .{tool});
            return 0;
        }
    }
    const host = targets.host() orelse {
        ctx.warn("no default target for this host, skipping configure", .{});
        return 0;
    };
    ctx.step("configuring for {s} ({s})", .{ host.name, host.triple });
    if (!try build.configureOnly(ctx, .{ .project = root, .targets = &.{host} })) {
        ctx.errorMessage("files were created, but configuring failed (see above)", .{});
        return 1;
    }
    ctx.print("    created  {s}\n", .{ctx.join(&.{ root, "compile_commands.json" })});
    return 0;
}

/// CMake target names allow letters, digits, `_`, `-`, `.` and `+`; anything
/// else in the directory name becomes `_`.
fn projectName(arena: std.mem.Allocator, base: []const u8) []const u8 {
    if (base.len == 0) return "app";
    const name = arena.dupe(u8, base) catch @panic("OOM");
    for (name) |*c| {
        if (!std.ascii.isAlphanumeric(c.*) and c.* != '_' and c.* != '-') c.* = '_';
    }
    return name;
}

test projectName {
    var arena_state: std.heap.ArenaAllocator = .init(std.testing.allocator);
    defer arena_state.deinit();
    const arena = arena_state.allocator();
    try std.testing.expectEqualStrings("hello", projectName(arena, "hello"));
    try std.testing.expectEqualStrings("my_app-2", projectName(arena, "my app-2"));
    try std.testing.expectEqualStrings("app", projectName(arena, ""));
}
