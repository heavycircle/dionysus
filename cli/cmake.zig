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
    /// What `zig version` printed. Objects built by another zig are not
    /// reused: a change starts the target over, as a new SDK does.
    zig: []const u8 = "",
    /// The prebuilt SDK to import (cli/prebuilt.zig), or "" to compile it.
    prebuilt: []const u8 = "",
    target: Target,
    mode: Mode,
    /// CMAKE_BUILD_TYPE when it isn't the mode's: one given with `--` stays
    /// until --debug or --release (cli/commands/build.zig).
    build_type: []const u8 = "",
    /// Absolute paths of the DINO_LIBRARY_PATH libraries.
    libraries: []const []const u8 = &.{},
    /// Extra arguments forwarded verbatim to the configure step.
    extra: []const []const u8 = &.{},
};

pub const stamp_file = ".dino-stamp";

pub fn buildType(cfg: Config) []const u8 {
    return if (cfg.build_type.len != 0) cfg.build_type else cfg.mode.cmakeName();
}

/// The `-D` settings that define a dino build. Anything that changes these
/// requires a fresh configure. The `--` arguments aren't among them: CMake's
/// cache keeps what they set, so a command without them changes nothing.
pub fn defines(ctx: *Ctx, cfg: Config) []const []const u8 {
    const sdk = cmakePath(ctx, cfg.sdk);
    var list: std.ArrayList([]const u8) = .empty;
    list.appendSlice(ctx.arena, &.{
        ctx.sprint("-DCMAKE_TOOLCHAIN_FILE={s}/cmake/toolchain.cmake", .{sdk}),
        ctx.sprint("-DCMAKE_PROJECT_INCLUDE={s}/cmake/Dino.cmake", .{sdk}),
        ctx.sprint("-DDINO_SDK_PATH={s}", .{sdk}),
        ctx.sprint("-DZIG_TARGET={s}", .{cfg.target.triple}),
        ctx.sprint("-DCMAKE_BUILD_TYPE={s}", .{buildType(cfg)}),
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    }) catch @panic("OOM");
    // Passed even when empty: a -D left out keeps its old value in CMakeCache.txt.
    list.append(ctx.arena, ctx.sprint("-DDINO_SDK_PREBUILT_DIR={s}", .{cmakePath(ctx, cfg.prebuilt)})) catch @panic("OOM");
    var paths: std.ArrayList([]const u8) = .empty;
    for (cfg.libraries) |lib| paths.append(ctx.arena, cmakePath(ctx, lib)) catch @panic("OOM");
    const joined = std.mem.join(ctx.arena, ";", paths.items) catch @panic("OOM");
    list.append(ctx.arena, ctx.sprint("-DDINO_LIBRARY_PATH={s}", .{joined})) catch @panic("OOM");
    // Lets ctest (and add_custom_command) run foreign binaries through qemu or wine.
    const emulator = if (targets.emulator(ctx, cfg.target)) |e| cmakePath(ctx, e) else "";
    list.append(ctx.arena, ctx.sprint("-DCMAKE_CROSSCOMPILING_EMULATOR={s}", .{emulator})) catch @panic("OOM");
    return list.items;
}

/// CMake treats backslashes in -D values as escapes.
pub fn cmakePath(ctx: *Ctx, path: []const u8) []const u8 {
    if (builtin.os.tag != .windows) return path;
    const copy = ctx.arena.dupe(u8, path) catch @panic("OOM");
    std.mem.replaceScalar(u8, copy, '\\', '/');
    return copy;
}

pub fn stamp(ctx: *Ctx, cfg: Config) []const u8 {
    return hash(ctx, defines(ctx, cfg));
}

fn hash(ctx: *Ctx, items: []const []const u8) []const u8 {
    var hasher = std.hash.Wyhash.init(0);
    for (items) |item| {
        hasher.update(item);
        hasher.update(&.{0});
    }
    return ctx.sprint("{x:0>16}", .{hasher.final()});
}

/// The stamp file holds one line each: the stamp, the SDK, the zig version, the
/// source and build directories the build dir was configured with, and a hash
/// of the last `--` arguments. It is empty while a configure runs, and stays
/// empty if it fails.
const Recorded = struct { stamp: []const u8, sdk: []const u8, zig: []const u8, source: []const u8, build: []const u8, extra: []const u8 };

fn recorded(ctx: *Ctx, cfg: Config) Recorded {
    const bytes = Io.Dir.cwd().readFileAlloc(ctx.io, ctx.join(&.{ cfg.build_dir, stamp_file }), ctx.arena, .limited(64 << 10)) catch "";
    var lines = std.mem.splitScalar(u8, bytes, '\n');
    return .{
        .stamp = lines.first(),
        .sdk = lines.next() orelse "",
        .zig = lines.next() orelse "",
        .source = lines.next() orelse "",
        .build = lines.next() orelse "",
        .extra = lines.next() orelse "",
    };
}

/// Hashed with the mtimes of the files CMake reads only on a first configure,
/// so editing an SDK checkout (DINO_SDK_PATH) takes effect too.
fn sdkIdentity(ctx: *Ctx, cfg: Config) []const u8 {
    var hasher = std.hash.Wyhash.init(0);
    for ([_][]const u8{ "toolchain.cmake", "flags.cmake" }) |name| {
        const stat = Io.Dir.cwd().statFile(ctx.io, ctx.join(&.{ cfg.sdk, "cmake", name }), .{}) catch continue;
        hasher.update(std.mem.asBytes(&stat.mtime));
    }
    return ctx.sprint("{s} {x:0>16}", .{ cfg.sdk, hasher.final() });
}

fn absoluteBuildDir(ctx: *Ctx, cfg: Config) []const u8 {
    return Io.Dir.cwd().realPathFileAlloc(ctx.io, cfg.build_dir, ctx.arena) catch "";
}

/// Configured with another SDK or zig, or in another place (the project was
/// moved or copied): CMake keeps the compiler it found and the objects it
/// built, and refuses a cache made for other directories, so only starting
/// over works.
fn startsOver(ctx: *Ctx, cfg: Config) bool {
    const r = recorded(ctx, cfg);
    return !std.mem.eql(u8, r.sdk, sdkIdentity(ctx, cfg)) or !std.mem.eql(u8, r.zig, cfg.zig) or
        !std.mem.eql(u8, r.source, cfg.source_dir) or !std.mem.eql(u8, r.build, absoluteBuildDir(ctx, cfg));
}

pub fn needsConfigure(ctx: *Ctx, cfg: Config) bool {
    if (!ctx.exists(ctx.join(&.{ cfg.build_dir, "CMakeCache.txt" }))) return true;
    if (startsOver(ctx, cfg)) return true;
    // What CMake generates, deleted by hand: only a configure brings it back.
    if (!ctx.exists(ctx.join(&.{ cfg.build_dir, ".cmake", "api", "v1", "reply" }))) return true;
    if (!ctx.exists(ctx.join(&.{ cfg.build_dir, "build.ninja" })) and !ctx.exists(ctx.join(&.{ cfg.build_dir, "Makefile" }))) return true;
    const r = recorded(ctx, cfg);
    if (!std.mem.eql(u8, r.stamp, stamp(ctx, cfg))) return true;
    return cfg.extra.len != 0 and !std.mem.eql(u8, r.extra, hash(ctx, cfg.extra));
}

/// The value of `name` in a CMakeCache.txt (`NAME:TYPE=value` lines).
pub fn cacheValue(cache: []const u8, name: []const u8) ?[]const u8 {
    var lines = std.mem.splitScalar(u8, cache, '\n');
    while (lines.next()) |raw| {
        const line = std.mem.trimEnd(u8, raw, "\r");
        if (!std.mem.startsWith(u8, line, name) or line.len == name.len or line[name.len] != ':') continue;
        const eq = std.mem.indexOfScalar(u8, line, '=') orelse continue;
        return line[eq + 1 ..];
    }
    return null;
}

test cacheValue {
    const cache = "# comment\nCMAKE_BUILD_TYPE_X:STRING=no\nCMAKE_BUILD_TYPE:STRING=Debug\r\nOTHER:BOOL=ON\n";
    try std.testing.expectEqualStrings("Debug", cacheValue(cache, "CMAKE_BUILD_TYPE").?);
    try std.testing.expectEqualStrings("ON", cacheValue(cache, "OTHER").?);
    try std.testing.expect(cacheValue(cache, "MISSING") == null);
}

pub const RunOptions = struct {
    verbose: bool = false,
    jobs: ?u32 = null,
    /// Build the dino_test() executables too, which `all` leaves out
    /// (cmake/Dino.cmake): `dino test` does, `dino build` and `dino run` don't.
    tests: bool = false,
};

pub fn configure(ctx: *Ctx, cfg: Config, opts: RunOptions) Ctx.Error!bool {
    const cwd = Io.Dir.cwd();
    const stamp_path = ctx.join(&.{ cfg.build_dir, stamp_file });
    const cache_path = ctx.join(&.{ cfg.build_dir, "CMakeCache.txt" });
    // Its settings are someone's; dino doesn't delete them.
    if (ctx.exists(cache_path) and !ctx.exists(stamp_path)) {
        return ctx.fail("{s} was configured without dino; remove it, or configure it in another directory", .{cfg.build_dir});
    }
    // An empty query file asks CMake to describe its targets, which is how
    // `dino run` finds executables without the project naming them for us.
    const query_dir = ctx.join(&.{ cfg.build_dir, ".cmake", "api", "v1", "query" });
    cwd.createDirPath(ctx.io, query_dir) catch |e| return ctx.fail("creating {s}: {t}", .{ query_dir, e });
    cwd.writeFile(ctx.io, .{ .sub_path = ctx.join(&.{ query_dir, "codemodel-v2" }), .data = "" }) catch |e|
        return ctx.fail("writing CMake file API query: {t}", .{e});

    // CMake reads the toolchain file only on the first configure, so a cache
    // made with another SDK keeps its compiler and flags. A failed configure
    // may have left the cache half-written, so it starts over too.
    const previous = recorded(ctx, cfg);
    const fresh = startsOver(ctx, cfg);
    if (fresh) {
        cwd.deleteFile(ctx.io, cache_path) catch |e| switch (e) {
            error.FileNotFound => {},
            else => return ctx.fail("removing the CMake cache in {s}: {t}", .{ cfg.build_dir, e }),
        };
        cwd.deleteTree(ctx.io, ctx.join(&.{ cfg.build_dir, "CMakeFiles" })) catch |e|
            return ctx.fail("removing {s}/CMakeFiles: {t}", .{ cfg.build_dir, e });
    }
    // Empty, it still marks the directory as dino's for `dino clean`.
    cwd.writeFile(ctx.io, .{ .sub_path = stamp_path, .data = "" }) catch |e|
        return ctx.fail("writing {s}: {t}", .{ stamp_path, e });

    var argv: std.ArrayList([]const u8) = .empty;
    argv.appendSlice(ctx.arena, &.{ "cmake", "-S", cfg.source_dir, "-B", cfg.build_dir }) catch @panic("OOM");
    if (!ctx.exists(cache_path)) {
        if (try generator(ctx)) |g| argv.appendSlice(ctx.arena, &.{ "-G", g }) catch @panic("OOM");
        // The only slow step a user sees with no output: zig builds the C
        // library for this target and build type the first time anything uses
        // it, and caches it for every later project.
        ctx.print("    configuring; the first build of a target can take a minute while zig builds its C library\n", .{});
    }
    if (!opts.verbose) argv.append(ctx.arena, "--log-level=WARNING") catch @panic("OOM");
    argv.appendSlice(ctx.arena, defines(ctx, cfg)) catch @panic("OOM");
    argv.appendSlice(ctx.arena, cfg.extra) catch @panic("OOM");

    if (opts.verbose) ctx.print("$ {s}\n", .{commandLine(ctx, argv.items)});
    if (try ctx.exec(argv.items, .{}) != 0) return false;

    // A build type given with `--` is what the next command keeps.
    var done = cfg;
    const cache = cwd.readFileAlloc(ctx.io, cache_path, ctx.arena, .limited(16 << 20)) catch "";
    if (cacheValue(cache, "CMAKE_BUILD_TYPE")) |t| done.build_type = if (std.mem.eql(u8, t, cfg.mode.cmakeName())) "" else t;
    // Without new `--` arguments, the cache still holds the last ones' values.
    const extra = if (cfg.extra.len != 0) hash(ctx, cfg.extra) else if (fresh) "" else previous.extra;
    const lines = [_][]const u8{ stamp(ctx, done), sdkIdentity(ctx, cfg), cfg.zig, cfg.source_dir, absoluteBuildDir(ctx, cfg), extra };
    const data = std.mem.concat(ctx.arena, u8, &.{ std.mem.join(ctx.arena, "\n", &lines) catch @panic("OOM"), "\n" }) catch @panic("OOM");
    cwd.writeFile(ctx.io, .{ .sub_path = stamp_path, .data = data }) catch |e|
        return ctx.fail("writing {s}: {t}", .{ stamp_path, e });
    return true;
}

/// Ninja when available; otherwise CMake's platform default, except on Windows
/// where that default (Visual Studio) can't use the zig toolchain.
pub fn generator(ctx: *Ctx) Ctx.Error!?[]const u8 {
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
    if (opts.tests) argv.appendSlice(ctx.arena, &.{ "--target", "all", tests_target }) catch @panic("OOM");
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
    /// Path relative to the current working directory, or absolute.
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

/// Where cmake/Dino.cmake builds DINO_LIBRARY_PATH libraries.
const library_build_dir = "_dino_libs/";

/// The FOLDER dino_test() puts test targets in (cmake/Dino.cmake).
const test_folder = "dino_tests";
/// The target that builds every dino_test() executable (cmake/Dino.cmake).
const tests_target = "dino_tests";

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
        // Executables of DINO_LIBRARY_PATH libraries (cmake/Dino.cmake) are never built here.
        if (std.mem.startsWith(u8, artifact, library_build_dir)) continue;
        list.append(ctx.arena, .{
            .name = info.name,
            // Absolute when CMAKE_RUNTIME_OUTPUT_DIRECTORY is outside the build dir.
            .path = if (std.fs.path.isAbsolute(artifact)) artifact else ctx.join(&.{ cfg.build_dir, artifact }),
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
