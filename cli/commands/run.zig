const std = @import("std");
const Ctx = @import("../Ctx.zig");
const Args = @import("../Args.zig");
const cmake = @import("../cmake.zig");
const targets = @import("../targets.zig");
const build = @import("build.zig");

pub const usage =
    \\Usage: dino run [target] [options] [-- program-args...]
    \\
    \\Build the project for one target, then run its executable. Binaries for
    \\other architectures run under qemu user-mode emulation; Windows binaries
    \\run under wine on Linux. Exits with the program's exit code.
    \\
    \\Options:
    \\    --exe <name>        Executable to run when the project defines several
    \\    --no-build          Run the existing binary without rebuilding
    \\    --debug             Debug build
    \\    --release           Release build (default)
    \\    -j, --jobs <n>      Parallel compile jobs
    \\    -C <dir>            Use <dir> as the project directory
    \\    -v, --verbose       Show CMake commands and full output
    \\    -h, --help          Show this message
    \\
;

pub fn main(ctx: *Ctx, raw: []const []const u8) Ctx.Error!u8 {
    var args: Args = .{ .items = raw };
    var opts: build.Options = .{};
    var target_name: ?[]const u8 = null;
    var exe_name: ?[]const u8 = null;
    var no_build = false;
    var program_args: []const []const u8 = &.{};
    while (args.next()) |arg| {
        if (Args.isHelp(arg)) {
            ctx.print("{s}", .{usage});
            return 0;
        } else if (std.mem.eql(u8, arg, "--")) {
            program_args = args.rest();
        } else if (std.mem.eql(u8, arg, "--no-build")) {
            no_build = true;
        } else if (try args.value(ctx, arg, &.{"--exe"})) |v| {
            exe_name = v;
        } else if (try build.parseCommon(ctx, &args, arg, &opts)) {
            continue;
        } else if (std.mem.startsWith(u8, arg, "-")) {
            return ctx.usage("unknown option '{s}'\n\n{s}", .{ arg, usage });
        } else if (target_name != null) {
            return ctx.usage("`dino run` takes a single target", .{});
        } else {
            target_name = arg;
        }
    }
    opts.targets = try build.resolveTargets(ctx, if (target_name) |n| &.{n} else &.{});
    const target = opts.targets[0];
    // Before building: a first build of a target can take a minute.
    const runner = try targets.requireRunner(ctx, target, "binaries");
    // A test is left out of the default build; one asked for by name has to
    // be built (with the others: they are one CMake target, dino_tests).
    if (exe_name != null) opts.run.tests = true;

    const executables = if (no_build)
        try existingExecutables(ctx, opts)
    else blk: {
        // stdout is the program's alone, so `dino run | jq` sees only its output.
        const out = ctx.out;
        ctx.out = ctx.err;
        ctx.child_stdout_to_stderr = true;
        const results = try build.buildTargets(ctx, opts);
        ctx.out = out;
        ctx.child_stdout_to_stderr = false;
        if (ctx.interruptedCode()) |code| return code;
        if (!results[0].ok) return 1;
        break :blk results[0].executables;
    };
    const exe = try pick(ctx, executables, exe_name, target, no_build);

    var argv: std.ArrayList([]const u8) = .empty;
    var env: ?*const std.process.Environ.Map = null;
    if (runner) |r| argv.append(ctx.arena, r) catch @panic("OOM");
    if (targets.runnerFor(target) == .wine) env = try quietWine(ctx);
    argv.append(ctx.arena, exe.path) catch @panic("OOM");
    argv.appendSlice(ctx.arena, program_args) catch @panic("OOM");
    return ctx.exec(argv.items, .{ .env = env });
}

fn existingExecutables(ctx: *Ctx, opts: build.Options) Ctx.Error![]const cmake.Executable {
    const target = opts.targets[0];
    const cfg: cmake.Config = .{
        .source_dir = opts.project,
        .build_dir = build.buildDir(ctx, opts.project, target),
        .sdk = "",
        .target = target,
        .mode = opts.mode,
    };
    const cache = std.Io.Dir.cwd().readFileAlloc(ctx.io, ctx.join(&.{ cfg.build_dir, "CMakeCache.txt" }), ctx.arena, .limited(16 << 20)) catch
        return ctx.fail("{s} has not been built yet; run `dino build {s}`", .{ target.name, target.name });
    // build/<target> holds one mode at a time, whichever was built last.
    const built = cmake.cacheValue(cache, "CMAKE_BUILD_TYPE") orelse "";
    if (opts.mode_given and !std.mem.eql(u8, built, opts.mode.cmakeName())) {
        return ctx.fail("{s} holds a {s} build, not {s}; run without --no-build to build it", .{ cfg.build_dir, built, opts.mode.cmakeName() });
    }
    return cmake.executables(ctx, cfg);
}

fn pick(ctx: *Ctx, executables: []const cmake.Executable, name: ?[]const u8, target: targets.Target, no_build: bool) Ctx.Error!cmake.Executable {
    if (name) |n| {
        for (executables) |exe| {
            if (!std.mem.eql(u8, exe.name, n)) continue;
            if (exe.built) return exe;
            if (no_build) return notBuilt(ctx, target);
            return ctx.fail("'{s}' is left out of the build (EXCLUDE_FROM_ALL), so {s} doesn't exist", .{ n, exe.path });
        }
        return ctx.fail("no executable named '{s}'; available: {s}", .{ n, names(ctx, executables) });
    }
    // Tests run through `dino test`; only an explicit --exe picks one here.
    // Programs the build leaves out (EXCLUDE_FROM_ALL) aren't candidates.
    var programs: std.ArrayList(cmake.Executable) = .empty;
    var defined = false;
    for (executables) |exe| {
        if (exe.is_test) continue;
        defined = true;
        if (exe.built) programs.append(ctx.arena, exe) catch @panic("OOM");
    }
    return switch (programs.items.len) {
        0 => if (defined and no_build) notBuilt(ctx, target) else ctx.fail("the project does not build any executables", .{}),
        1 => programs.items[0],
        else => ctx.fail("the project builds several executables; choose one with --exe ({s})", .{names(ctx, programs.items)}),
    };
}

fn notBuilt(ctx: *Ctx, target: targets.Target) Ctx.Error {
    return ctx.fail("{s} has not been built yet; run `dino build {s}`", .{ target.name, target.name });
}

fn names(ctx: *Ctx, executables: []const cmake.Executable) []const u8 {
    var list: std.ArrayList(u8) = .empty;
    for (executables, 0..) |exe, i| {
        if (i != 0) list.appendSlice(ctx.arena, ", ") catch @panic("OOM");
        list.appendSlice(ctx.arena, exe.name) catch @panic("OOM");
    }
    return list.items;
}

/// wine prints fixme/err chatter that drowns out the program's own output.
pub fn quietWine(ctx: *Ctx) Ctx.Error!*const std.process.Environ.Map {
    const env = ctx.arena.create(std.process.Environ.Map) catch @panic("OOM");
    env.* = ctx.env.clone(ctx.arena) catch @panic("OOM");
    if (env.get("WINEDEBUG") == null) env.put("WINEDEBUG", "-all") catch @panic("OOM");
    return env;
}
