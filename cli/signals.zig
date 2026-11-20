//! While a child runs, dino stays until it exits instead of dying first and
//! leaving it behind. Terminal signals (Ctrl-C, Ctrl-\) already reach the
//! child's process group, so dino only ignores those; signals sent to dino
//! alone (kill, a supervisor's SIGTERM, a closed terminal's SIGHUP) are passed
//! on to the child.
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

    old: [signals.len]linux.Sigaction = undefined,

    pub fn install() Posix {
        child.store(0, .release);
        pending.store(0, .release);
        last.store(0, .release);
        var g: Posix = .{};
        const action: linux.Sigaction = .{
            .handler = .{ .sigaction = handle },
            .mask = linux.sigemptyset(),
            .flags = linux.SA.SIGINFO | linux.SA.RESTART,
        };
        for (signals, &g.old) |sig, *old| _ = linux.sigaction(sig, &action, old);
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

    pub fn restore(g: *Posix) void {
        for (signals, &g.old) |sig, *old| _ = linux.sigaction(sig, old, null);
        child.store(0, .release);
    }

    fn handle(sig: SIG, info: *const linux.siginfo_t, _: ?*anyopaque) callconv(.c) void {
        last.store(@backingInt(sig), .release);
        // A positive code means the kernel sent it, for a terminal key.
        if ((sig == .INT or sig == .QUIT) and info.code > 0) return;
        const pid = child.load(.acquire);
        if (pid == 0) {
            pending.store(@backingInt(sig), .release);
        } else {
            _ = linux.kill(pid, sig);
        }
    }
};

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

    pub fn restore(_: *Windows) void {
        _ = SetConsoleCtrlHandler(handle, .FALSE);
    }

    fn handle(event: u32) callconv(.winapi) BOOL {
        if (event != CTRL_C_EVENT and event != CTRL_BREAK_EVENT) return .FALSE;
        interrupted.store(true, .release);
        return .TRUE;
    }
};
