//! Process-wide state shared by every command.
const std = @import("std");
const builtin = @import("builtin");
const Io = std.Io;
const signals = @import("signals.zig");

const Ctx = @This();

io: Io,
/// Everything dino allocates lives until exit, so one arena is enough.
arena: std.mem.Allocator,
env: *std.process.Environ.Map,
out: *Io.Writer,
err: *Io.Writer,
/// The signal that interrupted a child: commands stop instead of going on
/// to the next target, and exit 128 + this.
interrupted: ?u8 = null,
/// Children write their stdout to stderr: `dino run` keeps its stdout for
/// the program alone.
child_stdout_to_stderr: bool = false,
/// The last child's exit code in full: on Windows it is 32 bits, which the
/// u8 that exec returns can't hold (a crash is 0xC0000005).
last_status: u32 = 0,

/// Returned after a message has already been shown to the user. Usage is a
/// mistake on the command line (exit 2), Reported any other failure (exit 1).
pub const Error = error{ Reported, Usage };

pub fn print(ctx: *Ctx, comptime fmt: []const u8, args: anytype) void {
    ctx.out.print(fmt, args) catch {};
    ctx.out.flush() catch {};
}

pub fn step(ctx: *Ctx, comptime fmt: []const u8, args: anytype) void {
    ctx.print("==> " ++ fmt ++ "\n", args);
}

pub fn warn(ctx: *Ctx, comptime fmt: []const u8, args: anytype) void {
    ctx.out.flush() catch {};
    ctx.err.print("warning: " ++ fmt ++ "\n", args) catch {};
    ctx.err.flush() catch {};
}

pub fn errorMessage(ctx: *Ctx, comptime fmt: []const u8, args: anytype) void {
    ctx.out.flush() catch {};
    ctx.err.print("error: " ++ fmt ++ "\n", args) catch {};
    ctx.err.flush() catch {};
}

pub fn fail(ctx: *Ctx, comptime fmt: []const u8, args: anytype) Error {
    ctx.errorMessage(fmt, args);
    return error.Reported;
}

pub fn usage(ctx: *Ctx, comptime fmt: []const u8, args: anytype) Error {
    ctx.errorMessage(fmt, args);
    return error.Usage;
}

pub fn sprint(ctx: *Ctx, comptime format: []const u8, args: anytype) []u8 {
    return std.fmt.allocPrint(ctx.arena, format, args) catch @panic("OOM");
}

pub fn join(ctx: *Ctx, parts: []const []const u8) []u8 {
    return std.fs.path.join(ctx.arena, parts) catch @panic("OOM");
}

pub fn exists(ctx: *Ctx, path: []const u8) bool {
    Io.Dir.cwd().access(ctx.io, path, .{}) catch return false;
    return true;
}

/// Searches PATH like a shell would. Names containing a separator are checked as-is.
pub fn findProgram(ctx: *Ctx, name: []const u8) ?[]const u8 {
    if (std.mem.indexOfAny(u8, name, "/\\") != null) {
        return if (ctx.isExecutable(name)) name else null;
    }
    const path_var = ctx.env.get("PATH") orelse return null;
    var it = std.mem.tokenizeScalar(u8, path_var, std.fs.path.delimiter);
    while (it.next()) |dir| {
        const candidate = ctx.join(&.{ dir, name });
        if (builtin.os.tag != .windows) {
            if (ctx.isExecutable(candidate)) return candidate;
            continue;
        }
        // As Windows looks programs up: by the extensions in PATHEXT, never
        // an extensionless file (an MSYS or Git Bash shell script), which
        // CreateProcess can't start.
        if (std.fs.path.extension(name).len != 0 and ctx.isExecutable(candidate)) return candidate;
        var exts = std.mem.tokenizeScalar(u8, nonEmpty(ctx.env.get("PATHEXT")) orelse ".COM;.EXE;.BAT;.CMD", ';');
        while (exts.next()) |ext| {
            const with_ext = ctx.sprint("{s}{s}", .{ candidate, ext });
            if (ctx.isExecutable(with_ext)) return with_ext;
        }
    }
    return null;
}

fn nonEmpty(value: ?[]const u8) ?[]const u8 {
    const v = value orelse return null;
    return if (v.len == 0) null else v;
}

fn isExecutable(ctx: *Ctx, path: []const u8) bool {
    const stat = Io.Dir.cwd().statFile(ctx.io, path, .{}) catch return false;
    if (stat.kind == .directory) return false;
    if (builtin.os.tag == .windows) return true;
    Io.Dir.cwd().access(ctx.io, path, .{ .execute = true }) catch return false;
    return true;
}

pub const ExecOptions = struct {
    cwd: ?[]const u8 = null,
    env: ?*const std.process.Environ.Map = null,
};

/// Runs a child with inherited stdio and returns its exit status,
/// using the shell convention of 128 + signal for signalled children.
/// dino waits for it even when interrupted (see signals.zig).
pub fn exec(ctx: *Ctx, argv: []const []const u8, options: ExecOptions) Error!u8 {
    ctx.out.flush() catch {};
    ctx.err.flush() catch {};
    var guard: signals.Guard = .install();
    defer guard.restore();
    var child = std.process.spawn(ctx.io, .{
        .argv = argv,
        .cwd = if (options.cwd) |p| .{ .path = p } else .inherit,
        .environ_map = options.env,
        .stdout = if (ctx.child_stdout_to_stderr) .{ .file = Io.File.stderr() } else .inherit,
    }) catch |e| switch (e) {
        error.FileNotFound => return ctx.fail("{s} does not exist", .{argv[0]}),
        else => return ctx.fail("unable to start {s}: {t}", .{ argv[0], e }),
    };
    guard.forwardTo(child.id.?);
    // std reports only the low byte of a Windows exit code, so 256 would pass
    // for success: read the whole code before std's wait closes the handle.
    const full: ?u32 = if (builtin.os.tag == .windows) windowsExitCode(child.id.?) else null;
    const term = child.wait(ctx.io) catch |e| return ctx.fail("waiting for {s}: {t}", .{ argv[0], e });
    if (guard.received()) |sig| ctx.interrupted = sig;
    if (guard.forwarded()) |sig| signals.signalDescendants(ctx.io, ctx.arena, sig);
    const code = exitCode(term);
    ctx.last_status = full orelse code;
    return statusByte(ctx.last_status);
}

/// A failure stays a failure in one byte: codes that don't fit become 1.
fn statusByte(status: u32) u8 {
    return std.math.cast(u8, status) orelse 1;
}

test statusByte {
    try std.testing.expectEqual(@as(u8, 0), statusByte(0));
    try std.testing.expectEqual(@as(u8, 3), statusByte(3));
    try std.testing.expectEqual(@as(u8, 1), statusByte(256));
    try std.testing.expectEqual(@as(u8, 1), statusByte(0xC0000005));
}

const windows = std.os.windows;
extern "kernel32" fn WaitForSingleObject(handle: windows.HANDLE, milliseconds: u32) callconv(.winapi) u32;
extern "kernel32" fn GetExitCodeProcess(process: windows.HANDLE, code: *u32) callconv(.winapi) c_int;
extern "kernel32" fn ExitProcess(code: u32) callconv(.winapi) noreturn;

fn windowsExitCode(process: windows.HANDLE) ?u32 {
    const infinite = 0xffffffff;
    if (WaitForSingleObject(process, infinite) != 0) return null;
    var code: u32 = 0;
    if (GetExitCodeProcess(process, &code) == 0) return null;
    return code;
}

/// Exits with the last child's code in full, which on Windows may not fit
/// the u8 a command returns: `dino run` passes a program's 0xC0000005 on.
pub fn exitWithLastStatus(ctx: *Ctx, code: u8) u8 {
    if (builtin.os.tag == .windows and ctx.last_status != code) {
        ctx.out.flush() catch {};
        ctx.err.flush() catch {};
        ExitProcess(ctx.last_status);
    }
    return code;
}

/// The exit code for a command a signal interrupted, if one did.
pub fn interruptedCode(ctx: *Ctx) ?u8 {
    return if (ctx.interrupted) |sig| 128 +| sig else null;
}

pub const Captured = struct {
    code: u8,
    stdout: []const u8,
    stderr: []const u8,
};

pub fn capture(ctx: *Ctx, argv: []const []const u8) Error!Captured {
    const result = std.process.run(ctx.arena, ctx.io, .{
        .argv = argv,
        .stdout_limit = .limited(1 << 20),
        .stderr_limit = .limited(1 << 20),
    }) catch |e| return ctx.fail("unable to run {s}: {t}", .{ argv[0], e });
    return .{ .code = exitCode(result.term), .stdout = result.stdout, .stderr = result.stderr };
}

pub fn exitCode(term: std.process.Child.Term) u8 {
    return switch (term) {
        .exited => |code| code,
        .signal, .stopped => |sig| 128 +| @as(u8, @truncate(@backingInt(sig))),
        .unknown => 1,
    };
}
