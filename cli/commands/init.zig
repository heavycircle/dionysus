const std = @import("std");
const Io = std.Io;
const Ctx = @import("../Ctx.zig");
const Args = @import("../Args.zig");
const build = @import("build.zig");
const cmake = @import("../cmake.zig");
const targets = @import("../targets.zig");
const templates = @import("templates");

pub const usage =
    \\Usage: dino init [directory] [options]
    \\
    \\Create a new dino project in <directory> (default: the current directory).
    \\Existing files are never overwritten. The project is named after the
    \\directory, which can't be a name CMake or dino already uses (all, clean,
    \\help, install, package, test, test_main, dino_*, ...).
    \\
    \\In a directory that already has a CMakeLists.txt, only the files it lacks
    \\among .gitignore, .clang-format and .clang-tidy are added.
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
    \\    -C <dir>            Work from <dir>: <directory> is relative to it
    \\    -h, --help          Show this message
    \\
;

const cmake_template =
    \\cmake_minimum_required(VERSION 3.20)
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
    \\    dino_socket_t s = dino_socket_create(AF_INET, SOCK_STREAM, 0);
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
    var base: ?[]const u8 = null;
    var configure = true;
    while (args.next()) |arg| {
        if (Args.isHelp(arg)) {
            ctx.print("{s}", .{usage});
            return 0;
        } else if (std.mem.eql(u8, arg, "--no-configure")) {
            configure = false;
        } else if (try args.value(ctx, arg, &.{"-C"})) |v| {
            base = v;
        } else if (std.mem.startsWith(u8, arg, "-")) {
            return ctx.usage("unknown option '{s}'\n\n{s}", .{ arg, usage });
        } else if (dir != null) {
            return ctx.usage("`dino init` takes a single directory", .{});
        } else if (arg.len == 0) {
            return ctx.usage("the directory for `dino init` is empty", .{});
        } else {
            dir = arg;
        }
    }
    // -C as in the other commands and in git and make: an absolute directory
    // is not moved under it.
    const root = if (dir) |d|
        (if (base != null and !std.fs.path.isAbsolute(d)) ctx.join(&.{ base.?, d }) else d)
    else
        base orelse ".";
    const cwd = Io.Dir.cwd();
    // Named before anything is created, so a refused name leaves nothing behind.
    const here = cwd.realPathFileAlloc(ctx.io, ".", ctx.arena) catch |e|
        return ctx.fail("resolving the current directory: {t}", .{e});
    const abs = std.fs.path.resolve(ctx.arena, &.{ here, root }) catch @panic("OOM");
    const name = projectName(ctx.arena, std.fs.path.basename(abs));
    // A directory that has a CMakeLists.txt is a project already: it keeps its
    // name, whatever that is, and gets no second main() or sample test, which
    // its own CMakeLists.txt would pick up. Only the tooling files are added.
    const is_new = !ctx.exists(ctx.join(&.{ root, "CMakeLists.txt" }));
    if (is_new and isReserved(name)) {
        return ctx.usage("'{s}' can't be a project name: CMake or dino already has a target by that name; use a directory with another name", .{name});
    }
    // Before any file: a failure here must not leave a CMakeLists.txt behind,
    // which would make the next attempt take this for an existing project.
    if (is_new) {
        for ([_][]const u8{ "src", "tests" }) |sub| {
            const path = ctx.join(&.{ root, sub });
            cwd.createDirPath(ctx.io, path) catch |e| return ctx.fail("creating {s}: {t}", .{ path, e });
        }
    }

    const files = [_]struct { path: []const u8, data: []const u8, source: bool = false }{
        .{ .path = "CMakeLists.txt", .data = ctx.sprint(cmake_template, .{name}) },
        .{ .path = "src/main.c", .data = ctx.sprint(main_template, .{name}), .source = true },
        .{ .path = "tests/test_main.c", .data = ctx.sprint(test_template, .{}), .source = true },
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
        if (file.source and !is_new) {
            ctx.print("    skipped  {s} (the project already exists)\n", .{dest});
            continue;
        }
        cwd.writeFile(ctx.io, .{ .sub_path = dest, .data = file.data, .flags = .{ .exclusive = true } }) catch |e| switch (e) {
            // Something is there that exists() can't see through: a symbolic
            // link to a file that is gone.
            error.PathAlreadyExists => {
                ctx.print("    exists   {s}\n", .{dest});
                continue;
            },
            else => return ctx.fail("writing {s}: {t}", .{ dest, e }),
        };
        ctx.print("    created  {s}\n", .{dest});
    }

    const status: u8 = if (configure) try configureHost(ctx, root) else 0;

    ctx.print("\nNext:\n", .{});
    if (!std.mem.eql(u8, root, ".")) ctx.print("    {s}\n", .{cmake.commandLine(ctx, &.{ "cd", root })});
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
    // An error it has already reported (a bad DINO_LIBRARY_PATH entry, say)
    // is the same outcome: the project is there, the configure step isn't.
    const configured = build.configureOnly(ctx, .{ .project = root, .targets = &.{host} }) catch false;
    if (!configured) {
        ctx.errorMessage("files were created, but configuring failed (see above)", .{});
        return 1;
    }
    // A configure that worked may still have written none to export.
    const compdb = ctx.join(&.{ root, "compile_commands.json" });
    if (ctx.exists(compdb)) ctx.print("    created  {s}\n", .{compdb});
    return 0;
}

/// Letters, digits, `_` and `-` are kept; anything else in the directory name
/// becomes `_`. CMake would also take `.` and `+` in a target name, but the
/// name is the program's file name too.
fn projectName(arena: std.mem.Allocator, base: []const u8) []const u8 {
    if (base.len == 0) return "app";
    const name = arena.dupe(u8, base) catch @panic("OOM");
    for (name) |*c| {
        if (!std.ascii.isAlphanumeric(c.*) and c.* != '_' and c.* != '-') c.* = '_';
    }
    return name;
}

/// Targets CMake defines itself (see policy CMP0037), the test target the
/// generated CMakeLists.txt adds, the SDK's dino_ namespace, and the
/// directories CMake, CTest and Dino.cmake make in build/<target>/, where the
/// program of the same name would have to go.
const reserved = [_][]const u8{
    "all",        "clean",          "help",       "install",       "test",
    "package",    "package_source", "edit_cache", "rebuild_cache", "ALL_BUILD",
    "ZERO_CHECK", "RUN_TESTS",      "INSTALL",    "PACKAGE",       "list_install_components",
    "preinstall", "test_main",      "CMakeFiles", "Testing",       "_dino",
    "_dino_libs",
};

fn isReserved(name: []const u8) bool {
    if (std.mem.startsWith(u8, name, "dino_")) return true;
    for (reserved) |r| {
        if (std.mem.eql(u8, name, r)) return true;
    }
    return false;
}

test isReserved {
    try std.testing.expect(isReserved("test"));
    try std.testing.expect(isReserved("dino_core"));
    try std.testing.expect(isReserved("ZERO_CHECK"));
    try std.testing.expect(isReserved("preinstall"));
    try std.testing.expect(isReserved("_dino_libs"));
    try std.testing.expect(!isReserved("testing"));
    try std.testing.expect(!isReserved("dino"));
}

test projectName {
    var arena_state: std.heap.ArenaAllocator = .init(std.testing.allocator);
    defer arena_state.deinit();
    const arena = arena_state.allocator();
    try std.testing.expectEqualStrings("hello", projectName(arena, "hello"));
    try std.testing.expectEqualStrings("my_app-2", projectName(arena, "my app-2"));
    try std.testing.expectEqualStrings("app", projectName(arena, ""));
}

test "the .clang-format for projects has no key that needs clang-format 19" {
    try std.testing.expect(std.mem.indexOf(u8, templates.clang_format, "MainIncludeChar") == null);
    try std.testing.expect(std.mem.indexOf(u8, templates.clang_format, "SDK only") == null);
    // The rest is there, to the last line.
    try std.testing.expect(std.mem.startsWith(u8, templates.clang_format, "BasedOnStyle: WebKit\n"));
    try std.testing.expect(std.mem.indexOf(u8, templates.clang_format, "\nIncludeIsMainRegex:") != null);
    try std.testing.expect(std.mem.endsWith(u8, templates.clang_format, "    Priority: 1\n"));
}
