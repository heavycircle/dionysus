const std = @import("std");
const Io = std.Io;
const Ctx = @import("../Ctx.zig");
const Args = @import("../Args.zig");
const cmake = @import("../cmake.zig");
const compdb = @import("../compdb.zig");
const sdk_cache = @import("../sdk_cache.zig");
const targets = @import("../targets.zig");
const Target = targets.Target;

pub const usage =
    \\Usage: dino build [targets...] [options] [-- cmake-args...]
    \\
    \\Configure and build the CMake project in the current directory with the
    \\dino toolchain. Output goes to build/<target>/. With no target, builds for
    \\the host.
    \\
    \\Options:
    \\    --all               Build every known target (see `dino targets`)
    \\    --debug             Debug build (CMAKE_BUILD_TYPE=Debug)
    \\    --release           Release build (default)
    \\    -j, --jobs <n>      Parallel compile jobs (default: CPU count)
    \\    -C <dir>            Use <dir> as the project directory
    \\    -v, --verbose       Show CMake commands and full output
    \\    -h, --help          Show this message
    \\
    \\Arguments after `--` are passed to CMake's configure step, e.g.
    \\    dino build arm64 -- -DENABLE_LOGGING=ON
    \\
;

pub const Options = struct {
    project: []const u8 = ".",
    targets: []const Target = &.{},
    mode: cmake.Mode = .release,
    run: cmake.RunOptions = .{},
    extra: []const []const u8 = &.{},
};

pub const Result = struct {
    cfg: cmake.Config,
    ok: bool,
    executables: []const cmake.Executable = &.{},
};

pub fn main(ctx: *Ctx, raw: []const []const u8) Ctx.Error!u8 {
    var args: Args = .{ .items = raw };
    var opts: Options = .{};
    var names: std.ArrayList([]const u8) = .empty;
    var all = false;
    while (args.next()) |arg| {
        if (Args.isHelp(arg)) {
            ctx.print("{s}", .{usage});
            return 0;
        } else if (std.mem.eql(u8, arg, "--")) {
            opts.extra = args.rest();
        } else if (std.mem.eql(u8, arg, "--all")) {
            all = true;
        } else if (try parseCommon(ctx, &args, arg, &opts)) {
            continue;
        } else if (std.mem.startsWith(u8, arg, "-")) {
            return ctx.fail("unknown option '{s}'\n\n{s}", .{ arg, usage });
        } else {
            names.append(ctx.arena, arg) catch @panic("OOM");
        }
    }
    if (all and names.items.len != 0) return ctx.fail("--all cannot be combined with explicit targets", .{});
    opts.targets = if (all) &targets.known else try resolveTargets(ctx, names.items);

    const results = try buildTargets(ctx, opts);
    var failed: usize = 0;
    for (results) |r| {
        if (!r.ok) failed += 1;
    }
    if (results.len > 1) {
        ctx.print("\n", .{});
        ctx.step("summary: {d} succeeded, {d} failed", .{ results.len - failed, failed });
        for (results) |r| ctx.print("    {s:<8} {s}\n", .{ if (r.ok) "ok" else "FAILED", r.cfg.target.name });
    }
    return if (failed == 0) 0 else 1;
}

/// Flags shared by `build` and `run`. Returns true when `arg` was consumed.
pub fn parseCommon(ctx: *Ctx, args: *Args, arg: []const u8, opts: *Options) Ctx.Error!bool {
    if (std.mem.eql(u8, arg, "--debug")) {
        opts.mode = .debug;
    } else if (std.mem.eql(u8, arg, "--release")) {
        opts.mode = .release;
    } else if (std.mem.eql(u8, arg, "-v") or std.mem.eql(u8, arg, "--verbose")) {
        opts.run.verbose = true;
    } else if (try args.value(ctx, arg, &.{ "-j", "--jobs" })) |v| {
        const jobs = std.fmt.parseInt(u32, v, 10) catch 0;
        if (jobs == 0) return ctx.fail("invalid job count '{s}'", .{v});
        opts.run.jobs = jobs;
    } else if (try args.value(ctx, arg, &.{"-C"})) |v| {
        opts.project = v;
    } else {
        return false;
    }
    return true;
}

pub fn resolveTargets(ctx: *Ctx, names: []const []const u8) Ctx.Error![]const Target {
    if (names.len == 0) {
        const host = targets.host() orelse
            return ctx.fail("no default target for this host; pass one explicitly (see `dino targets`)", .{});
        return ctx.arena.dupe(Target, &.{host}) catch @panic("OOM");
    }
    const list = ctx.arena.alloc(Target, names.len) catch @panic("OOM");
    for (names, list) |name, *t| {
        t.* = targets.resolve(name) catch
            return ctx.fail("unknown target '{s}' (see `dino targets`)", .{name});
    }
    return list;
}

/// Builds each target in turn, continuing past failures so `--all` reports everything.
pub fn buildTargets(ctx: *Ctx, opts: Options) Ctx.Error![]const Result {
    const project = try setup(ctx, opts);

    const results = ctx.arena.alloc(Result, opts.targets.len) catch @panic("OOM");
    var exported = false;
    for (opts.targets, results) |target, *result| {
        const cfg = config(ctx, opts, project, target);
        result.* = .{ .cfg = cfg, .ok = false };
        ctx.step("{s} ({s}, {t})", .{ target.name, target.triple, opts.mode });

        if (cmake.needsConfigure(ctx, cfg) and !try cmake.configure(ctx, cfg, opts.run)) continue;
        // Before compiling: editors need the compile database most when the code doesn't build.
        if (!exported) {
            exported = true;
            exportCompileCommands(ctx, cfg, opts.project);
        }
        if (!try cmake.build(ctx, cfg, opts.run)) continue;

        result.executables = try cmake.executables(ctx, cfg);
        result.ok = true;
        for (result.executables) |exe| ctx.print("    {s} ({s})\n", .{ exe.path, size(ctx, exe.path) });
    }
    return results;
}

/// Configures the first target without building, so `dino init` leaves a
/// compile_commands.json behind before the project has ever been compiled.
pub fn configureOnly(ctx: *Ctx, opts: Options) Ctx.Error!bool {
    const project = try setup(ctx, opts);
    const cfg = config(ctx, opts, project, opts.targets[0]);
    if (cmake.needsConfigure(ctx, cfg) and !try cmake.configure(ctx, cfg, opts.run)) return false;
    exportCompileCommands(ctx, cfg, opts.project);
    return true;
}

const Project = struct { source_dir: []const u8, sdk: []const u8 };

fn setup(ctx: *Ctx, opts: Options) Ctx.Error!Project {
    try requireTools(ctx);
    if (!ctx.exists(ctx.join(&.{ opts.project, "CMakeLists.txt" }))) {
        return ctx.fail("no CMakeLists.txt in '{s}' (create one with `dino init`)", .{opts.project});
    }
    const source_dir = Io.Dir.cwd().realPathFileAlloc(ctx.io, opts.project, ctx.arena) catch |e|
        return ctx.fail("resolving project directory '{s}': {t}", .{ opts.project, e });
    return .{ .source_dir = source_dir, .sdk = try sdk_cache.path(ctx) };
}

fn config(ctx: *Ctx, opts: Options, project: Project, target: Target) cmake.Config {
    return .{
        .source_dir = project.source_dir,
        .build_dir = buildDir(ctx, opts.project, target),
        .sdk = project.sdk,
        .target = target,
        .mode = opts.mode,
        .extra = opts.extra,
    };
}

pub fn buildDir(ctx: *Ctx, project: []const u8, target: Target) []const u8 {
    if (std.mem.eql(u8, project, ".")) return ctx.join(&.{ "build", target.name });
    return ctx.join(&.{ project, "build", target.name });
}

pub const required_tools = [_][]const u8{ "cmake", "zig" };

fn requireTools(ctx: *Ctx) Ctx.Error!void {
    for (required_tools) |tool| {
        if (ctx.findProgram(tool) == null) {
            return ctx.fail("{s} was not found on PATH (run `dino doctor`)", .{tool});
        }
    }
}

/// clangd and similar tools look for compile_commands.json at the project root.
fn exportCompileCommands(ctx: *Ctx, cfg: cmake.Config, project: []const u8) void {
    const src = ctx.join(&.{ cfg.build_dir, "compile_commands.json" });
    if (!ctx.exists(src)) return;
    compdb.write(ctx, src, ctx.join(&.{ project, "compile_commands.json" }), cfg.target.triple);
}

fn size(ctx: *Ctx, path: []const u8) []const u8 {
    const stat = Io.Dir.cwd().statFile(ctx.io, path, .{}) catch return "missing";
    return formatSize(ctx.arena, stat.size);
}

fn formatSize(arena: std.mem.Allocator, bytes: u64) []const u8 {
    if (bytes < 1024) return std.fmt.allocPrint(arena, "{d} B", .{bytes}) catch @panic("OOM");
    const kib = @as(f64, @floatFromInt(bytes)) / 1024.0;
    if (kib < 1024) return std.fmt.allocPrint(arena, "{d:.1} KiB", .{kib}) catch @panic("OOM");
    return std.fmt.allocPrint(arena, "{d:.1} MiB", .{kib / 1024.0}) catch @panic("OOM");
}

test formatSize {
    var arena_state: std.heap.ArenaAllocator = .init(std.testing.allocator);
    defer arena_state.deinit();
    const arena = arena_state.allocator();
    try std.testing.expectEqualStrings("512 B", formatSize(arena, 512));
    try std.testing.expectEqualStrings("25.5 KiB", formatSize(arena, 26112));
    try std.testing.expectEqualStrings("1.5 MiB", formatSize(arena, 1572864));
}
