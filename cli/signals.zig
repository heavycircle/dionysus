//! While a child runs, dino stays until it exits instead of dying first and
//! leaving it behind. Signals the kernel sends (Ctrl-C, Ctrl-\, a closed
//! terminal's SIGHUP) already reach the child's process group, so dino only
//! notes those; signals sent to dino alone (kill, a supervisor's SIGTERM) are
//! passed on to the child, and then to whatever the child leaves running.
const std = @import("std");
const builtin = @import("builtin");

pub const Guard = switch (builtin.os.tag) {
    .linux => Posix,
    .windows => Windows,
    else => Noop,
};

const Noop = struct {
    pub fn install() Noop {
        return .{};
    }
    pub fn forwardTo(_: *Noop, _: std.process.Child.Id) void {}
    pub fn received(_: *Noop) ?u8 {
        return null;
    }
    pub fn forwarded(_: *Noop) ?u8 {
        return null;
    }
    pub fn restore(_: *Noop) void {}
};

const Posix = struct {
    const linux = std.os.linux;
    const SIG = linux.SIG;
    const signals = [_]SIG{ .INT, .QUIT, .TERM, .HUP };

    var child: std.atomic.Value(linux.pid_t) = .init(0);
    /// A signal that arrived before the child's pid was known.
    var pending: std.atomic.Value(u32) = .init(0);
    /// The last signal that arrived, forwarded or not.
    var last: std.atomic.Value(u32) = .init(0);
    /// The last signal dino passed on, which the child's own children never got.
    var passed_on: std.atomic.Value(u32) = .init(0);

    old: [signals.len]linux.Sigaction = undefined,
    /// Ignored when dino started (nohup, `cmd &` in a shell without job
    /// control) and left that way. A handler here would undo it for every
    /// child too: exec resets a caught signal to its default, not an ignored one.
    ignored: [signals.len]bool = @splat(false),

    pub fn install() Posix {
        child.store(0, .release);
        pending.store(0, .release);
        last.store(0, .release);
        passed_on.store(0, .release);
        // What the child leaves behind when it dies becomes dino's child, not
        // init's, so that signalDescendants can find it.
        _ = linux.prctl(@backingInt(linux.PR.SET_CHILD_SUBREAPER), 1, 0, 0, 0);
        var g: Posix = .{};
        const action: linux.Sigaction = .{
            .handler = .{ .sigaction = handle },
            .mask = linux.sigemptyset(),
            .flags = linux.SA.SIGINFO | linux.SA.RESTART,
        };
        for (signals, &g.old, &g.ignored) |sig, *old, *ignored| {
            _ = linux.sigaction(sig, null, old);
            ignored.* = old.handler.handler == SIG.IGN;
            if (!ignored.*) _ = linux.sigaction(sig, &action, null);
        }
        return g;
    }

    pub fn forwardTo(_: *Posix, pid: linux.pid_t) void {
        child.store(pid, .release);
        const sig = pending.swap(0, .acq_rel);
        if (sig != 0) _ = linux.kill(pid, @fromBackingInt(@intCast(sig)));
    }

    pub fn received(_: *Posix) ?u8 {
        const sig = last.load(.acquire);
        return if (sig == 0) null else @intCast(sig);
    }

    /// The signal dino passed on to the child, if it did: one that reached
    /// only dino, so nothing below the child has seen it.
    pub fn forwarded(_: *Posix) ?u8 {
        const sig = passed_on.load(.acquire);
        return if (sig == 0) null else @intCast(sig);
    }

    pub fn restore(g: *Posix) void {
        for (signals, &g.old, g.ignored) |sig, *old, ignored| {
            if (!ignored) _ = linux.sigaction(sig, old, null);
        }
        child.store(0, .release);
    }

    fn handle(sig: SIG, info: *const linux.siginfo_t, _: ?*anyopaque) callconv(.c) void {
        last.store(@backingInt(sig), .release);
        const pid = child.load(.acquire);
        // A positive code means the kernel sent it: a terminal key, or the
        // hangup of a closed terminal. Those go to the whole foreground
        // process group, so a running child has it already; one not yet
        // started doesn't, and gets it from forwardTo like any other.
        if (info.code > 0 and pid != 0) return;
        passed_on.store(@backingInt(sig), .release);
        if (pid == 0) {
            pending.store(@backingInt(sig), .release);
        } else {
            _ = linux.kill(pid, sig);
        }
    }
};

/// Sends `sig` to every process below dino. Called once the child has exited
/// of a signal dino passed on: the child got it, but a `cmake --build` or a
/// `ctest` that dies of it leaves make, the compilers and the tests running.
/// They are dino's descendants now (see install), and listed in /proc.
pub fn signalDescendants(io: std.Io, arena: std.mem.Allocator, sig: u8) void {
    if (builtin.os.tag != .linux) return;
    const linux = std.os.linux;
    const Process = struct { pid: linux.pid_t, parent: linux.pid_t };

    var all: std.ArrayList(Process) = .empty;
    var proc = std.Io.Dir.cwd().openDir(io, "/proc", .{ .iterate = true }) catch return;
    defer proc.close(io);
    var it = proc.iterate();
    while (it.next(io) catch return) |entry| {
        const pid = std.fmt.parseInt(linux.pid_t, entry.name, 10) catch continue;
        // Read with the system calls: a /proc file reports a size of 0, which
        // is all that a read sized from the file's metadata then returns.
        var name: [32]u8 = undefined;
        const path = std.fmt.bufPrintSentinel(&name, "/proc/{d}/stat", .{pid}, 0) catch continue;
        const fd = linux.open(path, .{ .ACCMODE = .RDONLY, .CLOEXEC = true }, 0);
        if (@as(isize, @bitCast(fd)) < 0) continue;
        var buffer: [1024]u8 = undefined;
        const len = linux.read(@intCast(fd), &buffer, buffer.len);
        _ = linux.close(@intCast(fd));
        if (@as(isize, @bitCast(len)) <= 0) continue;
        const stat = buffer[0..len];
        // "pid (name) state ppid ..."; the name may itself contain ") ".
        const after_name = std.mem.lastIndexOf(u8, stat, ") ") orelse continue;
        var fields = std.mem.tokenizeScalar(u8, stat[after_name + 2 ..], ' ');
        _ = fields.next() orelse continue;
        const parent = std.fmt.parseInt(linux.pid_t, fields.next() orelse continue, 10) catch continue;
        all.append(arena, .{ .pid = pid, .parent = parent }) catch return;
    }

    var below: std.ArrayList(linux.pid_t) = .empty;
    below.append(arena, linux.getpid()) catch return;
    var i: usize = 0;
    while (i < below.items.len) : (i += 1) {
        for (all.items) |p| {
            if (p.parent == below.items[i]) below.append(arena, p.pid) catch return;
        }
    }
    for (below.items[1..]) |pid| _ = linux.kill(pid, @fromBackingInt(@intCast(sig)));
}

/// Every process on the console gets Ctrl-C and Ctrl-Break, so dino only
/// ignores them; the child decides for itself.
const Windows = struct {
    const BOOL = std.os.windows.BOOL;
    const HandlerRoutine = *const fn (u32) callconv(.winapi) BOOL;
    extern "kernel32" fn SetConsoleCtrlHandler(handler: ?HandlerRoutine, add: BOOL) callconv(.winapi) BOOL;

    const CTRL_C_EVENT = 0;
    const CTRL_BREAK_EVENT = 1;

    var interrupted: std.atomic.Value(bool) = .init(false);

    pub fn install() Windows {
        interrupted.store(false, .release);
        _ = SetConsoleCtrlHandler(handle, .TRUE);
        return .{};
    }

    pub fn forwardTo(_: *Windows, _: std.process.Child.Id) void {}

    /// Reported as SIGINT, so dino exits 130 as on Linux.
    pub fn received(_: *Windows) ?u8 {
        return if (interrupted.load(.acquire)) 2 else null;
    }

    pub fn forwarded(_: *Windows) ?u8 {
        return null;
    }

    pub fn restore(_: *Windows) void {
        _ = SetConsoleCtrlHandler(handle, .FALSE);
    }

    fn handle(event: u32) callconv(.winapi) BOOL {
        if (event != CTRL_C_EVENT and event != CTRL_BREAK_EVENT) return .FALSE;
        interrupted.store(true, .release);
        return .TRUE;
    }
};
