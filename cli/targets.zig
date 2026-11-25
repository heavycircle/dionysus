const std = @import("std");
const builtin = @import("builtin");
const Ctx = @import("Ctx.zig");

const Arch = std.Target.Cpu.Arch;
const Os = std.Target.Os.Tag;

pub const Target = struct {
    /// Short name used on the command line and as the build directory name.
    name: []const u8,
    /// Passed to `zig cc -target`.
    triple: []const u8,
    description: []const u8,
    arch: Arch,
    os: Os,
    abi: ?std.Target.Abi = null,

    pub fn exeSuffix(t: Target) []const u8 {
        return if (t.os == .windows) ".exe" else "";
    }
};

pub const known = [_]Target{
    .{ .name = "arm", .triple = "arm-linux-musleabihf", .description = "ARMv7-A 32-bit, hard float", .arch = .arm, .os = .linux },
    .{ .name = "arm64", .triple = "aarch64-linux-musl", .description = "ARM 64-bit", .arch = .aarch64, .os = .linux },
    .{ .name = "i386", .triple = "x86-linux-musl", .description = "Intel x86 32-bit, i686", .arch = .x86, .os = .linux },
    .{ .name = "ppc", .triple = "powerpc-linux-musleabihf", .description = "PowerPC 32-bit, big endian", .arch = .powerpc, .os = .linux },
    .{ .name = "win32", .triple = "x86-windows-gnu", .description = "Windows PE32 (x86)", .arch = .x86, .os = .windows },
    .{ .name = "win64", .triple = "x86_64-windows-gnu", .description = "Windows PE32+ (x86_64)", .arch = .x86_64, .os = .windows },
    .{ .name = "x86_64", .triple = "x86_64-linux-musl", .description = "AMD64", .arch = .x86_64, .os = .linux },
};

/// valgrind can't see the allocator inside a static binary, so --memcheck
/// builds the tests against dynamic glibc and runs each one under valgrind,
/// for the host's own architecture: x86_64, or aarch64 (the arm64 image).
pub const memcheck: Target = if (builtin.cpu.arch == .aarch64) .{
    .name = "memcheck",
    .triple = "aarch64-linux-gnu",
    .description = "AArch64 Linux, dynamically linked, for valgrind",
    .arch = .aarch64,
    .os = .linux,
} else .{
    .name = "memcheck",
    .triple = "x86_64-linux-gnu",
    .description = "x86_64 Linux, dynamically linked, for valgrind",
    .arch = .x86_64,
    .os = .linux,
};

pub const ResolveError = error{UnknownTarget};

/// Other spellings of the short names, as other tools write them.
const aliases = [_]struct { []const u8, []const u8 }{
    .{ "aarch64", "arm64" },
    .{ "amd64", "x86_64" },
    .{ "i686", "i386" },
};

/// Accepts a known short name or alias in any case or, as an escape hatch,
/// any `arch-os[-abi]` triple zig understands.
pub fn resolve(name: []const u8) ResolveError!Target {
    for (known) |t| {
        if (std.ascii.eqlIgnoreCase(t.name, name)) return t;
    }
    for (aliases) |a| {
        if (std.ascii.eqlIgnoreCase(a[0], name)) return resolve(a[1]);
    }
    for (known) |t| {
        if (std.mem.eql(u8, t.triple, name)) return t;
    }
    if (std.mem.indexOfScalar(u8, name, '-') == null) return error.UnknownTarget;
    const query = std.Target.Query.parse(.{ .arch_os_abi = name }) catch return error.UnknownTarget;
    return .{
        .name = name,
        .triple = name,
        .description = "custom zig target",
        .arch = query.cpu_arch orelse return error.UnknownTarget,
        .os = query.os_tag orelse return error.UnknownTarget,
        .abi = query.abi,
    };
}

pub fn host() ?Target {
    return hostFor(builtin.os.tag, builtin.cpu.arch);
}

fn hostFor(os: Os, arch: Arch) ?Target {
    for (known) |t| {
        if (t.os == os and t.arch == arch) return t;
    }
    // Windows on ARM runs x86_64 programs.
    if (os == .windows and arch == .aarch64) return resolve("win64") catch unreachable;
    return null;
}

pub const Runner = union(enum) {
    native,
    /// Base name of the qemu user-mode emulator, e.g. "qemu-aarch64".
    qemu: []const u8,
    wine,
    unsupported,
};

pub fn runnerFor(t: Target) Runner {
    return runnerOn(t, builtin.os.tag, builtin.cpu.arch, runsAarch32());
}

var aarch32: ?bool = null;

/// Whether this aarch64 Linux machine runs 32-bit ARM binaries: many newer
/// cores (and Apple silicon) can't. arm64 kernels refuse the PER_LINUX32
/// personality exactly when no core can, so try it and switch back.
fn runsAarch32() bool {
    if (builtin.os.tag != .linux or builtin.cpu.arch != .aarch64) return false;
    if (aarch32) |cached| return cached;
    const linux = std.os.linux;
    const per_linux32 = 0x0008;
    const query = 0xffffffff;
    const old = linux.syscall1(.personality, query);
    const ok = linux.errno(linux.syscall1(.personality, per_linux32)) == .SUCCESS;
    _ = linux.syscall1(.personality, old);
    aarch32 = ok;
    return ok;
}

/// The installed program that runs `t`'s binaries on this host (qemu or wine),
/// or null when they run natively or no runner is installed.
pub fn emulator(ctx: *Ctx, t: Target) ?[]const u8 {
    return switch (runnerFor(t)) {
        .native, .unsupported => null,
        .qemu => |name| ctx.findProgram(ctx.sprint("{s}-static", .{name})) orelse ctx.findProgram(name),
        .wine => ctx.findProgram("wine"),
    };
}

/// The way `t`'s binaries are run, failing with a message naming `what`
/// ("binaries", "tests") when that can't be done here. Returns the program
/// to run them with, or null when they run directly.
pub fn requireRunner(ctx: *Ctx, t: Target, what: []const u8) Ctx.Error!?[]const u8 {
    return switch (runnerFor(t)) {
        .native => null,
        .qemu => |name| emulator(ctx, t) orelse
            ctx.fail("{s} is needed to run {s} {s}; install qemu-user (or use the dino Docker image)", .{ name, t.name, what }),
        .wine => emulator(ctx, t) orelse
            ctx.fail("wine is needed to run {s} {s} on Linux; install wine (or use the dino Docker image)", .{ t.name, what }),
        .unsupported => ctx.fail("{s} {s} cannot be run on this host", .{ t.name, what }),
    };
}

fn runnerOn(t: Target, host_os: Os, host_arch: Arch, host_aarch32: bool) Runner {
    // x32 needs a kernel option that distributions leave off, and qemu has no
    // user-mode emulator for it.
    if (t.abi) |abi| if (abi == .gnux32 or abi == .muslx32) return .unsupported;
    if (t.os == .windows and (host_os == .windows or host_os == .linux)) {
        // wine runs Windows programs for the machine's own architecture.
        const runs = t.arch == host_arch or (host_arch == .x86_64 and t.arch == .x86) or
            (host_os == .windows and host_arch == .aarch64 and (t.arch == .x86 or t.arch == .x86_64));
        if (!runs) return .unsupported;
        return if (host_os == .windows) .native else .wine;
    }
    if (t.os != host_os) return .unsupported;
    if (t.arch == host_arch) return .native;
    // 64-bit x86 kernels run 32-bit binaries directly.
    if (host_arch == .x86_64 and t.arch == .x86) return .native;
    if (host_arch == .aarch64 and t.arch == .arm and host_os == .linux and host_aarch32) return .native;
    if (t.os == .linux) return .{ .qemu = qemuName(t.arch) orelse return .unsupported };
    return .unsupported;
}

fn qemuName(arch: Arch) ?[]const u8 {
    return switch (arch) {
        .arm => "qemu-arm",
        .armeb => "qemu-armeb",
        .aarch64 => "qemu-aarch64",
        .x86 => "qemu-i386",
        .x86_64 => "qemu-x86_64",
        .powerpc => "qemu-ppc",
        .powerpc64 => "qemu-ppc64",
        .powerpc64le => "qemu-ppc64le",
        .mips => "qemu-mips",
        .mipsel => "qemu-mipsel",
        .mips64 => "qemu-mips64",
        .mips64el => "qemu-mips64el",
        .riscv32 => "qemu-riscv32",
        .riscv64 => "qemu-riscv64",
        .loongarch64 => "qemu-loongarch64",
        .sparc64 => "qemu-sparc64",
        .m68k => "qemu-m68k",
        .s390x => "qemu-s390x",
        else => null,
    };
}

test "resolve known names and triples" {
    try std.testing.expectEqualStrings("aarch64-linux-musl", (try resolve("arm64")).triple);
    try std.testing.expectEqualStrings("win64", (try resolve("x86_64-windows-gnu")).name);
    try std.testing.expectError(error.UnknownTarget, resolve("bogus"));
    try std.testing.expectError(error.UnknownTarget, resolve("notanarch-linux"));
    try std.testing.expectEqualStrings("arm64", (try resolve("aarch64")).name);
    try std.testing.expectEqualStrings("arm64", (try resolve("ARM64")).name);
    try std.testing.expectEqualStrings("x86_64", (try resolve("AMD64")).name);
    try std.testing.expectEqualStrings("i386", (try resolve("i686")).name);
    try std.testing.expectEqualStrings("win64", (try resolve("Win64")).name);
}

test "resolve custom triple" {
    const t = try resolve("riscv64-linux-musl");
    try std.testing.expectEqual(Arch.riscv64, t.arch);
    try std.testing.expectEqual(Os.linux, t.os);
    try std.testing.expectEqualStrings("riscv64-linux-musl", t.name);
}

test "host target" {
    try std.testing.expectEqualStrings("x86_64", hostFor(.linux, .x86_64).?.name);
    try std.testing.expectEqualStrings("win64", hostFor(.windows, .x86_64).?.name);
    try std.testing.expect(hostFor(.macos, .aarch64) == null);
    try std.testing.expectEqualStrings("win64", hostFor(.windows, .aarch64).?.name);
}

test "runner selection" {
    const r = struct {
        fn of(name: []const u8, os: Os, arch: Arch) Runner {
            return runnerOn(resolve(name) catch unreachable, os, arch, true);
        }
    };
    try std.testing.expectEqual(Runner.native, r.of("x86_64", .linux, .x86_64));
    try std.testing.expectEqual(Runner.native, r.of("i386", .linux, .x86_64));
    try std.testing.expectEqualStrings("qemu-aarch64", r.of("arm64", .linux, .x86_64).qemu);
    try std.testing.expectEqualStrings("qemu-ppc", r.of("ppc", .linux, .x86_64).qemu);
    try std.testing.expectEqual(Runner.wine, r.of("win64", .linux, .x86_64));
    try std.testing.expectEqual(Runner.native, r.of("win32", .windows, .x86_64));
    try std.testing.expectEqual(Runner.unsupported, r.of("x86_64", .macos, .aarch64));
    try std.testing.expectEqual(Runner.native, r.of("arm", .linux, .aarch64));
    try std.testing.expectEqualStrings("qemu-arm", runnerOn(try resolve("arm"), .linux, .aarch64, false).qemu);
    try std.testing.expectEqual(Runner.unsupported, r.of("x86_64-linux-muslx32", .linux, .x86_64));
    try std.testing.expectEqual(Runner.unsupported, r.of("x86_64-linux-gnux32", .linux, .x86_64));
    try std.testing.expectEqual(Runner.unsupported, r.of("win64", .linux, .aarch64));
    try std.testing.expectEqual(Runner.unsupported, r.of("aarch64-windows-gnu", .linux, .x86_64));
    try std.testing.expectEqual(Runner.wine, r.of("aarch64-windows-gnu", .linux, .aarch64));
    try std.testing.expectEqual(Runner.native, r.of("win64", .windows, .aarch64));
    try std.testing.expectEqualStrings("qemu-mips64el", r.of("mips64el-linux-musl", .linux, .x86_64).qemu);
}
