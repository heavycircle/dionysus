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
    \\    --timeout <seconds> Stop a test that runs longer and fail it (default: 120).
    \\                        A test's own TIMEOUT property in CMake takes precedence
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

/// ctest's own default is 1500 s, which a hang costs once per target.
const default_timeout_s = 120;

pub fn main(ctx: *Ctx, raw: []const []const u8) Ctx.Error!u8 {
    var args: Args = .{ .items = raw };
    var opts: build.Options = .{};
    var names: std.ArrayList([]const u8) = .empty;
    var all = false;
    var memcheck = false;
    var filter: ?[]const u8 = null;
    var timeout_s: u32 = default_timeout_s;
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
        } else if (try args.value(ctx, arg, &.{"--timeout"})) |v| {
            timeout_s = Args.count(v, std.math.maxInt(u32)) orelse return ctx.usage("invalid timeout '{s}'", .{v});
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
        if (opts.mode_given and opts.mode == .release) return ctx.usage("--memcheck builds Debug, for valgrind's stack traces; it takes no --release", .{});
        if (targets.runnerFor(targets.memcheck) != .native) return ctx.fail("--memcheck needs an x86_64 or aarch64 Linux host", .{});
        const valgrind = ctx.findProgram("valgrind") orelse
            return ctx.fail("--memcheck needs valgrind; install it (or use the dino Docker image)", .{});
        opts.targets = &.{targets.memcheck};
        // Symbols for valgrind's stack traces; the counter would only duplicate valgrind.
        opts.mode = .debug;
        opts.export_compdb = false;
        opts.extra = std.mem.concat(ctx.arena, []const u8, &.{ &.{
            ctx.sprint("-DCMAKE_CROSSCOMPILING_EMULATOR={s};--quiet;--error-exitcode=99;--leak-check=full;--errors-for-leak-kinds=definite,indirect;--track-origins=yes", .{valgrind}),
            "-DDINO_DYNAMIC=ON",
            "-DDINO_NO_LEAK_CHECK=ON",
        }, opts.extra }) catch @panic("OOM");
    } else {
        if (all and names.items.len != 0) return ctx.usage("--all cannot be combined with explicit targets", .{});
        opts.targets = if (all) &targets.known else try build.resolveTargets(ctx, names.items);
        for (opts.targets) |t| {
            if (all and targets.runnerFor(t) == .unsupported) continue;
            _ = try targets.requireRunner(ctx, t, "tests");
        }
    }

    opts.run.tests = true;
    const results = try build.buildTargets(ctx, opts);
    const Outcome = enum { ok, failed, skipped };
    const outcomes = ctx.arena.alloc(Outcome, results.len) catch @panic("OOM");
    var failed: usize = 0;
    var skipped: usize = 0;
    for (results, outcomes) |r, *outcome| {
        outcome.* = if (r.ok) .ok else .failed;
        if (ctx.interrupted != null) break;
        if (r.ok and targets.runnerFor(r.cfg.target) == .unsupported) {
            // Only --all gets here: a named target was refused up front.
            ctx.step("skipping {s} tests: this host can't run {s} binaries", .{ r.cfg.target.name, r.cfg.target.name });
            outcome.* = .skipped;
        } else if (r.ok) {
            ctx.step("testing {s}", .{r.cfg.target.name});
            if (!try ctest(ctx, r.cfg.build_dir, r.cfg.target, opts, filter, timeout_s, ctest_args)) outcome.* = .failed;
        }
        switch (outcome.*) {
            .failed => failed += 1,
            .skipped => skipped += 1,
            .ok => {},
        }
    }
    if (ctx.interruptedCode()) |code| return code;
    if (results.len > 1) {
        ctx.print("\n", .{});
        ctx.step("summary: {d} passed, {d} failed, {d} skipped", .{ results.len - failed - skipped, failed, skipped });
        for (results, outcomes) |r, outcome| {
            const label = switch (outcome) {
                .ok => "ok",
                .failed => "FAILED",
                .skipped => "skipped",
            };
            ctx.print("    {s:<8} {s}\n", .{ label, r.cfg.target.name });
        }
    }
    return if (failed == 0) 0 else 1;
}

/// Checked before building so a missing qemu or wine fails in seconds, not
/// after compiling every target.
fn ctest(
    ctx: *Ctx,
    build_dir: []const u8,
    t: Target,
    opts: build.Options,
    filter: ?[]const u8,
    timeout_s: u32,
    extra: []const []const u8,
) Ctx.Error!bool {
    const jobs = opts.run.jobs orelse @as(u32, @intCast(std.Thread.getCpuCount() catch 1));
    var argv: std.ArrayList([]const u8) = .empty;
    argv.appendSlice(ctx.arena, &.{ "ctest", "--output-on-failure", "--no-tests=error", "-j", ctx.sprint("{d}", .{jobs}) }) catch @panic("OOM");
    if (filter) |f| argv.appendSlice(ctx.arena, &.{ "-R", f }) catch @panic("OOM");
    // Before `extra`: ctest takes the last --timeout, so one after `--` still wins.
    argv.appendSlice(ctx.arena, &.{ "--timeout", ctx.sprint("{d}", .{timeout_s}) }) catch @panic("OOM");
    argv.appendSlice(ctx.arena, extra) catch @panic("OOM");
    const env = if (targets.runnerFor(t) == .wine) try run.quietWine(ctx) else null;
    return try ctx.exec(argv.items, .{ .cwd = build_dir, .env = env }) == 0;
}
