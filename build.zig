const std = @import("std");
const zon = @import("build.zig.zon");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{ .preferred_optimize_mode = .ReleaseSmall });

    const options = b.addOptions();
    options.addOption([]const u8, "version", zon.version);

    const mod = b.createModule(.{
        .root_source_file = b.path("cli/main.zig"),
        .target = target,
        .optimize = optimize,
        .imports = &.{
            .{ .name = "build_options", .module = options.createModule() },
            .{ .name = "sdk", .module = sdkModule(b) },
        },
    });

    const exe = b.addExecutable(.{ .name = "dino", .root_module = mod });
    b.installArtifact(exe);

    const run = b.addRunArtifact(exe);
    run.step.dependOn(b.getInstallStep());
    run.addPassthruArgs();
    b.step("run", "Run dino").dependOn(&run.step);

    const tests = b.addTest(.{ .root_module = mod });
    b.step("test", "Run unit tests").dependOn(&b.addRunArtifact(tests).step);

    // ZLS runs this step on save to report errors that need full semantic analysis.
    const check = b.addExecutable(.{ .name = "dino", .root_module = mod });
    b.step("check", "Compile without installing").dependOn(&check.step);
}

/// @embedFile cannot reach outside a module's directory, so sdk/ is copied next
/// to a generated index that embeds every file the CLI needs to unpack.
fn sdkModule(b: *std.Build) *std.Build.Module {
    const io = b.graph.io;
    const embedded = [_][]const u8{ "CMakeLists.txt", "cmake", "include", "src" };

    const sdk_root = b.root.join(b.allocator, "sdk") catch @panic("OOM");
    var sdk_dir = sdk_root.root_dir.handle.openDir(io, sdk_root.sub_path, .{ .iterate = true }) catch |err|
        std.debug.panic("unable to open sdk/: {t}", .{err});
    defer sdk_dir.close(io);

    // Directory listings are read at configure time; registering each visited
    // directory keeps the configure cache valid until a file is added or removed.
    b.dependOnDirectoryContents(b.path("sdk"));

    var paths: std.ArrayList([]const u8) = .empty;
    var walker = sdk_dir.walkSelectively(b.allocator) catch @panic("OOM");
    defer walker.deinit();
    while (walker.next(io) catch |err| std.debug.panic("walking sdk/: {t}", .{err})) |entry| {
        if (entry.depth() == 1) {
            for (embedded) |name| {
                if (std.mem.eql(u8, entry.basename, name)) break;
            } else continue;
        }
        const path = b.allocator.dupe(u8, entry.path) catch @panic("OOM");
        std.mem.replaceScalar(u8, path, std.fs.path.sep, '/');
        switch (entry.kind) {
            .directory => {
                b.dependOnDirectoryContents(b.path(b.fmt("sdk/{s}", .{path})));
                walker.enter(io, entry) catch |err| std.debug.panic("entering sdk/{s}: {t}", .{ path, err });
            },
            .file => paths.append(b.allocator, path) catch @panic("OOM"),
            else => {},
        }
    }
    std.mem.sort([]const u8, paths.items, {}, struct {
        fn lessThan(_: void, lhs: []const u8, rhs: []const u8) bool {
            return std.mem.lessThan(u8, lhs, rhs);
        }
    }.lessThan);

    var index: std.ArrayList(u8) = .empty;
    index.appendSlice(b.allocator,
        \\pub const File = struct { path: []const u8, data: []const u8 };
        \\pub const files = [_]File{
        \\
    ) catch @panic("OOM");
    for (paths.items) |path| {
        index.print(b.allocator, "    .{{ .path = \"{f}\", .data = @embedFile(\"sdk/{f}\") }},\n", .{
            std.zig.fmtString(path), std.zig.fmtString(path),
        }) catch @panic("OOM");
    }
    index.appendSlice(b.allocator, "};\n") catch @panic("OOM");

    const wf = b.addWriteFiles();
    _ = wf.addCopyDirectory(b.path("sdk"), "sdk", .{});
    return b.createModule(.{ .root_source_file = wf.add("sdk.zig", index.items) });
}
