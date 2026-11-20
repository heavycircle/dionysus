const std = @import("std");
const Ctx = @import("../Ctx.zig");
const Args = @import("../Args.zig");
const build = @import("build.zig");
const run = @import("run.zig");
const targets = @import("../targets.zig");
const Target = targets.Target;

pub const usage =
    \\Usage: dino test [targets...] [options] [-- ctest-args...]
    \\
    \\Build the project, then run its tests (dino_test() targets) with CTest.
    \\Foreign targets run under qemu-user or wine. With no target, tests the host.
    \\
    \\On Linux targets every test also fails if it leaks: an allocation counter
    \\is linked in, and anything still allocated at exit is reported.
    \\
    \\Options:
    \\    --all               Test every known target (see `dino targets`)
    \\    --memcheck          Run the tests under valgrind instead, in a separate
    \\                        dynamically linked x86_64 Linux build (host only):
    \\                        catches invalid reads/writes and use of uninitialized
    \\                        memory as well as leaks
    \\    -R <regex>          Only run tests whose name matches <regex>
    \\    --debug             Debug build (CMAKE_BUILD_TYPE=Debug)
    \\    --release           Release build (default)
    \\    -j, --jobs <n>      Parallel compile and test jobs (default: CPU count)
    \\    -C <dir>            Use <dir> as the project directory
    \\    -v, --verbose       Show CMake commands and full output
    \\    -h, --help          Show this message
    \\
    \\Arguments after `--` are passed to ctest, e.g.
    \\    dino test -- --repeat until-fail:10
    \\
;

pub fn main(ctx: *Ctx, raw: []const []const u8) Ctx.Error!u8 {
    var args: Args = .{ .items = raw };
    var opts: build.Options = .{};
    var names: std.ArrayList([]const u8) = .empty;
    var all = false;
    var memcheck = false;
    var filter: ?[]const u8 = null;
    var ctest_args: []const []const u8 = &.{};
    while (args.next()) |arg| {
        if (Args.isHelp(arg)) {
            ctx.print("{s}", .{usage});
            return 0;
        } else if (std.mem.eql(u8, arg, "--")) {
            ctest_args = args.rest();
        } else if (std.mem.eql(u8, arg, "--all")) {
            all = true;
        } else if (std.mem.eql(u8, arg, "--memcheck")) {
            memcheck = true;
        } else if (try args.value(ctx, arg, &.{"-R"})) |v| {
            filter = v;
        } else if (try build.parseCommon(ctx, &args, arg, &opts)) {
            continue;
        } else if (std.mem.startsWith(u8, arg, "-")) {
            return ctx.usage("unknown option '{s}'\n\n{s}", .{ arg, usage });
        } else {
            names.append(ctx.arena, arg) catch @panic("OOM");
        }
    }

    if (memcheck) {
        if (all or names.items.len != 0) return ctx.usage("--memcheck runs on the host only; it takes no targets", .{});
        if (targets.runnerFor(targets.memcheck) != .native) return ctx.fail("--memcheck needs an x86_64 Linux host", .{});
        const valgrind = ctx.findProgram("valgrind") orelse
            return ctx.fail("--memcheck needs valgrind; install it (or use the dino Docker image)", .{});
        opts.targets = &.{targets.memcheck};
        // Symbols for valgrind's stack traces; the counter would only duplicate valgrind.
        opts.mode = .debug;
        opts.extra = std.mem.concat(ctx.arena, []const u8, &.{ &.{
            ctx.sprint("-DCMAKE_CROSSCOMPILING_EMULATOR={s};--quiet;--error-exitcode=99;--leak-check=full;--errors-for-leak-kinds=definite,indirect;--track-origins=yes", .{valgrind}),
            "-DDINO_DYNAMIC=ON",
            "-DDINO_NO_LEAK_CHECK=ON",
        }, opts.extra }) catch @panic("OOM");
    } else {
        if (all and names.items.len != 0) return ctx.usage("--all cannot be combined with explicit targets", .{});
        opts.targets = if (all) &targets.known else try build.resolveTargets(ctx, names.items);
        for (opts.targets) |t| try requireRunner(ctx, t);
    }

    const results = try build.buildTargets(ctx, opts);
    const passed = ctx.arena.alloc(bool, results.len) catch @panic("OOM");
    var failed: usize = 0;
    for (results, passed) |r, *ok| {
        ok.* = r.ok;
        if (ctx.interrupted != null) break;
        if (r.ok) {
            ctx.step("testing {s}", .{r.cfg.target.name});
            ok.* = try ctest(ctx, r.cfg.build_dir, r.cfg.target, opts, filter, ctest_args);
        }
        if (!ok.*) failed += 1;
    }
    if (ctx.interruptedCode()) |code| return code;
    if (results.len > 1) {
        ctx.print("\n", .{});
        ctx.step("summary: {d} passed, {d} failed", .{ results.len - failed, failed });
        for (results, passed) |r, ok| ctx.print("    {s:<8} {s}\n", .{ if (ok) "ok" else "FAILED", r.cfg.target.name });
    }
    return if (failed == 0) 0 else 1;
}

/// Checked before building so a missing qemu or wine fails in seconds, not
/// after compiling every target.
fn requireRunner(ctx: *Ctx, t: Target) Ctx.Error!void {
    switch (targets.runnerFor(t)) {
        .native => {},
        .qemu => |name| if (targets.emulator(ctx, t) == null)
            return ctx.fail("{s} is needed to run {s} tests; install qemu-user (or use the dino Docker image)", .{ name, t.name }),
        .wine => if (targets.emulator(ctx, t) == null)
            return ctx.fail("wine is needed to run {s} tests on Linux; install wine (or use the dino Docker image)", .{t.name}),
        .unsupported => return ctx.fail("{s} tests cannot be run on this host", .{t.name}),
    }
}

fn ctest(
    ctx: *Ctx,
    build_dir: []const u8,
    t: Target,
    opts: build.Options,
    filter: ?[]const u8,
    extra: []const []const u8,
) Ctx.Error!bool {
    const jobs = opts.run.jobs orelse @as(u32, @intCast(std.Thread.getCpuCount() catch 1));
    var argv: std.ArrayList([]const u8) = .empty;
    argv.appendSlice(ctx.arena, &.{ "ctest", "--output-on-failure", "--no-tests=error", "-j", ctx.sprint("{d}", .{jobs}) }) catch @panic("OOM");
    if (filter) |f| argv.appendSlice(ctx.arena, &.{ "-R", f }) catch @panic("OOM");
    argv.appendSlice(ctx.arena, extra) catch @panic("OOM");
    const env = if (targets.runnerFor(t) == .wine) try run.quietWine(ctx) else null;
    return try ctx.exec(argv.items, .{ .cwd = build_dir, .env = env }) == 0;
}
