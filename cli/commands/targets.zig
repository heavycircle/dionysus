const std = @import("std");
const Ctx = @import("../Ctx.zig");
const Args = @import("../Args.zig");
const targets = @import("../targets.zig");

pub const usage =
    \\Usage: dino targets
    \\
    \\List the targets dino knows by name. Any other zig target triple
    \\(e.g. riscv64-linux-musl) can also be passed to `dino build`.
    \\
;

pub fn main(ctx: *Ctx, raw: []const []const u8) Ctx.Error!u8 {
    for (raw) |arg| {
        if (Args.isHelp(arg)) {
            ctx.print("{s}", .{usage});
            return 0;
        }
        return ctx.usage("unknown argument '{s}'\n\n{s}", .{ arg, usage });
    }
    const host = targets.host();
    ctx.print("  {s:<8} {s:<26} {s:<14} {s}\n", .{ "NAME", "ZIG TARGET", "RUNS VIA", "DESCRIPTION" });
    for (targets.known) |t| {
        const is_host = if (host) |h| std.mem.eql(u8, h.name, t.name) else false;
        ctx.print("{s} {s:<8} {s:<26} {s:<14} {s}\n", .{
            if (is_host) "*" else " ",
            t.name,
            t.triple,
            runnerLabel(targets.runnerFor(t)),
            t.description,
        });
    }
    if (host != null) ctx.print("\n* default target on this machine\n", .{});
    return 0;
}

pub fn runnerLabel(r: targets.Runner) []const u8 {
    return switch (r) {
        .native => "native",
        .qemu => |name| name,
        .wine => "wine",
        .unsupported => "-",
    };
}
