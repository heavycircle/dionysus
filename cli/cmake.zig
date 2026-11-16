//! Drives CMake with the dino toolchain and reads back what it built.
const std = @import("std");
const builtin = @import("builtin");
const Io = std.Io;
const Ctx = @import("Ctx.zig");
const Target = @import("targets.zig").Target;
const targets = @import("targets.zig");

pub const Mode = enum {
    debug,
    release,

    pub fn cmakeName(m: Mode) []const u8 {
        return switch (m) {
            .debug => "Debug",
            .release => "Release",
        };
    }
};

pub const Config = struct {
    /// Project source directory (holds CMakeLists.txt).
    source_dir: []const u8,
    build_dir: []const u8,
    sdk: []const u8,
    target: Target,
    mode: Mode,
    /// Extra arguments forwarded verbatim to the configure step.
    extra: []const []const u8 = &.{},
};

const stamp_file = ".dino-stamp";

/// The `-D` settings that define a dino build. Anything that changes these
/// requires a fresh configure.
pub fn defines(ctx: *Ctx, cfg: Config) []const []const u8 {
    const sdk = cmakePath(ctx, cfg.sdk);
    var list: std.ArrayList([]const u8) = .empty;
    list.appendSlice(ctx.arena, &.{
        ctx.sprint("-DCMAKE_TOOLCHAIN_FILE={s}/cmake/toolchain.cmake", .{sdk}),
        ctx.sprint("-DCMAKE_PROJECT_INCLUDE={s}/cmake/Dino.cmake", .{sdk}),
        ctx.sprint("-DDINO_SDK_PATH={s}", .{sdk}),
        ctx.sprint("-DZIG_TARGET={s}", .{cfg.target.triple}),
        ctx.sprint("-DCMAKE_BUILD_TYPE={s}", .{cfg.mode.cmakeName()}),
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    }) catch @panic("OOM");
    // Lets ctest (and add_custom_command) run foreign binaries through qemu or wine.
    if (targets.emulator(ctx, cfg.target)) |e| {
        list.append(ctx.arena, ctx.sprint("-DCMAKE_CROSSCOMPILING_EMULATOR={s}", .{cmakePath(ctx, e)})) catch @panic("OOM");
    }
    list.appendSlice(ctx.arena, cfg.extra) catch @panic("OOM");
    return list.items;
}

/// CMake treats backslashes in -D values as escapes.
fn cmakePath(ctx: *Ctx, path: []const u8) []const u8 {
    if (builtin.os.tag != .windows) return path;
    const copy = ctx.arena.dupe(u8, path) catch @panic("OOM");
    std.mem.replaceScalar(u8, copy, '\\', '/');
    return copy;
}

pub fn stamp(ctx: *Ctx, cfg: Config) []const u8 {
    var hasher = std.hash.Wyhash.init(0);
    hasher.update(cfg.source_dir);
    for (defines(ctx, cfg)) |d| {
        hasher.update(d);
        hasher.update(&.{0});
    }
    return ctx.sprint("{x:0>16}\n", .{hasher.final()});
}

pub fn needsConfigure(ctx: *Ctx, cfg: Config) bool {
    if (!ctx.exists(ctx.join(&.{ cfg.build_dir, "CMakeCache.txt" }))) return true;
    const previous = Io.Dir.cwd().readFileAlloc(ctx.io, ctx.join(&.{ cfg.build_dir, stamp_file }), ctx.arena, .limited(64)) catch return true;
    return !std.mem.eql(u8, previous, stamp(ctx, cfg));
}

pub const RunOptions = struct {
    verbose: bool = false,
    jobs: ?u32 = null,
};

pub fn configure(ctx: *Ctx, cfg: Config, opts: RunOptions) Ctx.Error!bool {
    const cwd = Io.Dir.cwd();
    // An empty query file asks CMake to describe its targets, which is how
    // `dino run` finds executables without the project naming them for us.
    const query_dir = ctx.join(&.{ cfg.build_dir, ".cmake", "api", "v1", "query" });
    cwd.createDirPath(ctx.io, query_dir) catch |e| return ctx.fail("creating {s}: {t}", .{ query_dir, e });
    cwd.writeFile(ctx.io, .{ .sub_path = ctx.join(&.{ query_dir, "codemodel-v2" }), .data = "" }) catch |e|
        return ctx.fail("writing CMake file API query: {t}", .{e});

    var argv: std.ArrayList([]const u8) = .empty;
    argv.appendSlice(ctx.arena, &.{ "cmake", "-S", cfg.source_dir, "-B", cfg.build_dir }) catch @panic("OOM");
    if (!ctx.exists(ctx.join(&.{ cfg.build_dir, "CMakeCache.txt" }))) {
        if (try generator(ctx)) |g| argv.appendSlice(ctx.arena, &.{ "-G", g }) catch @panic("OOM");
    }
    if (!opts.verbose) argv.append(ctx.arena, "--log-level=WARNING") catch @panic("OOM");
    argv.appendSlice(ctx.arena, defines(ctx, cfg)) catch @panic("OOM");

    if (opts.verbose) ctx.print("$ {s}\n", .{commandLine(ctx, argv.items)});
    if (try ctx.exec(argv.items, .{}) != 0) return false;

    cwd.writeFile(ctx.io, .{ .sub_path = ctx.join(&.{ cfg.build_dir, stamp_file }), .data = stamp(ctx, cfg) }) catch |e|
        return ctx.fail("writing {s}: {t}", .{ stamp_file, e });
    return true;
}

/// Ninja when available; otherwise CMake's platform default, except on Windows
/// where that default (Visual Studio) can't use the zig toolchain.
fn generator(ctx: *Ctx) Ctx.Error!?[]const u8 {
    if (ctx.findProgram("ninja") != null) return "Ninja";
    if (builtin.os.tag == .windows) {
        return ctx.fail("ninja is required on Windows (https://ninja-build.org)", .{});
    }
    if (ctx.findProgram("make") == null and ctx.findProgram("gmake") == null) {
        return ctx.fail("neither ninja nor make was found on PATH", .{});
    }
    return null;
}

pub fn build(ctx: *Ctx, cfg: Config, opts: RunOptions) Ctx.Error!bool {
    const jobs = opts.jobs orelse @as(u32, @intCast(std.Thread.getCpuCount() catch 1));
    var argv: std.ArrayList([]const u8) = .empty;
    argv.appendSlice(ctx.arena, &.{ "cmake", "--build", cfg.build_dir, "--parallel", ctx.sprint("{d}", .{jobs}) }) catch @panic("OOM");
    if (opts.verbose) {
        argv.append(ctx.arena, "--verbose") catch @panic("OOM");
        ctx.print("$ {s}\n", .{commandLine(ctx, argv.items)});
    }
    return try ctx.exec(argv.items, .{}) == 0;
}

fn commandLine(ctx: *Ctx, argv: []const []const u8) []const u8 {
    return std.mem.join(ctx.arena, " ", argv) catch @panic("OOM");
}

pub const Executable = struct {
    name: []const u8,
    /// Path relative to the current working directory.
    path: []const u8,
    /// Defined with dino_test(); `dino run` skips these unless asked for by name.
    is_test: bool = false,
};

const Index = struct {
    reply: struct {
        @"codemodel-v2": ?struct { jsonFile: ?[]const u8 = null } = null,
    },
};

const Codemodel = struct {
    configurations: []const struct {
        targets: []const struct { name: []const u8, jsonFile: []const u8 },
    },
};

const Artifact = struct { path: []const u8 };

const TargetInfo = struct {
    name: []const u8,
    type: []const u8,
    artifacts: []const Artifact = &.{},
    folder: ?struct { name: []const u8 } = null,
};

/// The FOLDER dino_test() puts test targets in (cmake/Dino.cmake).
const test_folder = "dino_tests";

const parse_options: std.json.ParseOptions = .{ .ignore_unknown_fields = true };

/// Executables the project defines, read from the CMake file API reply.
pub fn executables(ctx: *Ctx, cfg: Config) Ctx.Error![]const Executable {
    const reply_dir = ctx.join(&.{ cfg.build_dir, ".cmake", "api", "v1", "reply" });
    const index_name = try newestIndex(ctx, reply_dir);
    const index = try readJson(ctx, Index, ctx.join(&.{ reply_dir, index_name }));
    const codemodel_file = (index.reply.@"codemodel-v2" orelse return ctx.fail("CMake did not answer the codemodel query", .{})).jsonFile orelse
        return ctx.fail("CMake reported an error for the codemodel query", .{});
    const codemodel = try readJson(ctx, Codemodel, ctx.join(&.{ reply_dir, codemodel_file }));
    if (codemodel.configurations.len == 0) return &.{};

    var list: std.ArrayList(Executable) = .empty;
    for (codemodel.configurations[0].targets) |t| {
        const info = try readJson(ctx, TargetInfo, ctx.join(&.{ reply_dir, t.jsonFile }));
        if (!std.mem.eql(u8, info.type, "EXECUTABLE")) continue;
        const artifact = pickArtifact(info.artifacts, cfg.target.exeSuffix()) orelse continue;
        list.append(ctx.arena, .{
            .name = info.name,
            .path = ctx.join(&.{ cfg.build_dir, artifact }),
            .is_test = if (info.folder) |f| std.mem.eql(u8, f.name, test_folder) else false,
        }) catch @panic("OOM");
    }
    return list.items;
}

fn pickArtifact(artifacts: []const Artifact, suffix: []const u8) ?[]const u8 {
    for (artifacts) |a| {
        if (std.mem.endsWith(u8, a.path, suffix) and !std.mem.endsWith(u8, a.path, ".pdb")) return a.path;
    }
    return null;
}

fn newestIndex(ctx: *Ctx, reply_dir: []const u8) Ctx.Error![]const u8 {
    var dir = Io.Dir.cwd().openDir(ctx.io, reply_dir, .{ .iterate = true }) catch
        return ctx.fail("no CMake file API reply in {s}; reconfigure with `dino build`", .{reply_dir});
    defer dir.close(ctx.io);
    var newest: ?[]const u8 = null;
    var it = dir.iterate();
    while (it.next(ctx.io) catch |e| return ctx.fail("reading {s}: {t}", .{ reply_dir, e })) |entry| {
        if (!std.mem.startsWith(u8, entry.name, "index-") or !std.mem.endsWith(u8, entry.name, ".json")) continue;
        // Index names embed a timestamp, so the lexically greatest is the newest.
        if (newest == null or std.mem.order(u8, entry.name, newest.?) == .gt) {
            newest = ctx.arena.dupe(u8, entry.name) catch @panic("OOM");
        }
    }
    return newest orelse ctx.fail("no CMake file API index in {s}", .{reply_dir});
}

fn readJson(ctx: *Ctx, comptime T: type, path: []const u8) Ctx.Error!T {
    const bytes = Io.Dir.cwd().readFileAlloc(ctx.io, path, ctx.arena, .limited(16 << 20)) catch |e|
        return ctx.fail("reading {s}: {t}", .{ path, e });
    return parseJson(T, ctx.arena, bytes) catch ctx.fail("unexpected CMake file API data in {s}", .{path});
}

fn parseJson(comptime T: type, arena: std.mem.Allocator, bytes: []const u8) !T {
    return std.json.parseFromSliceLeaky(T, arena, bytes, parse_options);
}

test "parse file API replies" {
    var arena_state: std.heap.ArenaAllocator = .init(std.testing.allocator);
    defer arena_state.deinit();
    const arena = arena_state.allocator();

    const index = try parseJson(Index, arena,
        \\{"cmake":{"version":{"major":3}},"objects":[],
        \\ "reply":{"codemodel-v2":{"kind":"codemodel","version":{"major":2,"minor":7},
        \\ "jsonFile":"codemodel-v2-abc.json"}}}
    );
    try std.testing.expectEqualStrings("codemodel-v2-abc.json", index.reply.@"codemodel-v2".?.jsonFile.?);

    const failed = try parseJson(Index, arena,
        \\{"reply":{"codemodel-v2":{"error":"unknown request kind"}}}
    );
    try std.testing.expect(failed.reply.@"codemodel-v2".?.jsonFile == null);

    const model = try parseJson(Codemodel, arena,
        \\{"configurations":[{"name":"Release","directories":[],
        \\ "targets":[{"name":"app","id":"app::@6890","directoryIndex":0,"jsonFile":"target-app.json"}]}]}
    );
    try std.testing.expectEqualStrings("target-app.json", model.configurations[0].targets[0].jsonFile);

    const target = try parseJson(TargetInfo, arena,
        \\{"name":"app","type":"EXECUTABLE","nameOnDisk":"app.exe",
        \\ "artifacts":[{"path":"app.pdb"},{"path":"app.exe"}],"sources":[]}
    );
    try std.testing.expectEqualStrings("app.exe", pickArtifact(target.artifacts, ".exe").?);

    const lib = try parseJson(TargetInfo, arena,
        \\{"name":"dino_core","type":"STATIC_LIBRARY","artifacts":[{"path":"_dino/libdino_core.a"}]}
    );
    try std.testing.expectEqualStrings("STATIC_LIBRARY", lib.type);
}
