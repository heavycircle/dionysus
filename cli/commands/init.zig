const std = @import("std");
const Io = std.Io;
const Ctx = @import("../Ctx.zig");
const Args = @import("../Args.zig");
const build = @import("build.zig");
const targets = @import("../targets.zig");
const templates = @import("templates");

pub const usage =
    \\Usage: dino init [directory] [options]
    \\
    \\Create a new dino project in <directory> (default: the current directory).
    \\Existing files are never overwritten.
    \\
    \\Creates:
    \\    CMakeLists.txt         Builds every .c file under src/ and links the SDK
    \\    src/main.c             A hello-world program to start from
    \\    tests/test_main.c      A first test (utest.h); run with `dino test`
    \\    .gitignore             Ignores build output
    \\    .clang-format          The SDK's formatting rules, for clang-format and editors
    \\    .clang-tidy            The SDK's clang-tidy checks, for clangd and clang-tidy
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
    \\# dino_executable() is add_executable() plus linking the dino SDK (dino_core, dino_util).
    \\dino_executable({0s} ${{SOURCES}})
    \\target_include_directories({0s} PRIVATE src)
    \\# Libraries listed in DINO_LIBRARY_PATH are linked by target name, e.g.
    \\# target_link_libraries({0s} PRIVATE org::config)
    \\
    \\# Every tests/test_*.c is a test executable (utest.h); `dino test` runs them.
    \\file(GLOB TESTS CONFIGURE_DEPENDS tests/test_*.c)
    \\foreach(test ${{TESTS}})
    \\    get_filename_component(test_name ${{test}} NAME_WE)
    \\    dino_test(${{test_name}} ${{test}})
    \\endforeach()
    \\
;

const test_template =
    \\#include <utest.h>
    \\
    \\#include <dino/core/network.h>
    \\
    \\UTEST(app, creates_a_socket)
    \\{{
    \\    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    \\    EXPECT_NE(DINO_INVALID_SOCKET, s);
    \\    dino_socket_close(s);
    \\}}
    \\
    \\UTEST_MAIN()
    \\
;

const main_template =
    \\#include <stdio.h>
    \\
    \\int main(void)
    \\{{
    \\    puts("Hello from {0s}!");
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
            return ctx.usage("unknown option '{s}'\n\n{s}", .{ arg, usage });
        } else if (dir != null) {
            return ctx.usage("`dino init` takes a single directory", .{});
        } else {
            dir = arg;
        }
    }
    const root = dir orelse ".";
    const cwd = Io.Dir.cwd();
    for ([_][]const u8{ "src", "tests" }) |sub| cwd.createDirPath(ctx.io, ctx.join(&.{ root, sub })) catch |e|
        return ctx.fail("creating {s}: {t}", .{ root, e });
    const abs = cwd.realPathFileAlloc(ctx.io, root, ctx.arena) catch |e|
        return ctx.fail("resolving {s}: {t}", .{ root, e });
    const name = projectName(ctx.arena, std.fs.path.basename(abs));

    const files = [_]struct { path: []const u8, data: []const u8 }{
        .{ .path = "CMakeLists.txt", .data = ctx.sprint(cmake_template, .{name}) },
        .{ .path = "src/main.c", .data = ctx.sprint(main_template, .{name}) },
        .{ .path = "tests/test_main.c", .data = ctx.sprint(test_template, .{}) },
        .{ .path = ".gitignore", .data = gitignore_template },
        .{ .path = ".clang-format", .data = templates.clang_format },
        .{ .path = ".clang-tidy", .data = templates.clang_tidy },
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
    ctx.print("    dino test         # build and run tests/\n", .{});
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
