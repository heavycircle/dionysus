# Editors

Any editor that speaks the language server protocol gets completion, go-to-definition, inline errors
and the [portability warnings](portability-warnings.md) through **clangd**, using the
`compile_commands.json` that `dino` keeps at the project root.

## compile_commands.json

`dino init` writes it, and every `dino build`, `run` and `test` refreshes it before compiling, so it
exists even while the code doesn't build. It describes the **first target** of the command, so
`dino build win64` switches the editor to the Windows view of the code, `#ifdef _WIN32` branches and
MinGW headers included, and `dino build` switches it back to the host.

It isn't CMake's own file. CMake writes every entry as `zig cc -target ...`, which clang tools can't
parse: they would fall back to the host's own headers. `dino` rewrites it into plain clang commands
that name the target's real musl or MinGW headers, so the editor sees exactly what the build
compiles.

It's machine-specific (it holds absolute paths), so the generated `.gitignore` leaves it out of
version control.

## clangd

Install clangd from your distribution (`apt install clangd`) or let your editor's clangd extension
download one. Any version works, Ubuntu 22.04's clangd 14 included: the warnings come from the
compiler, and the `.clang-tidy` that `dino init` writes is readable by old versions too.

- **VS Code:** install the
  [clangd extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd),
  and disable IntelliSense from Microsoft's C/C++ extension if you have it (the clangd extension
  offers to), so you don't get two sets of errors.
- **Neovim:** enable clangd with `vim.lsp.enable('clangd')` (0.11+) or nvim-lspconfig's
  `require('lspconfig').clangd.setup({})`.
- **Emacs:** eglot (built in since Emacs 29) and lsp-mode both use clangd for C by default.
- **Zed, Helix, Kate, Sublime (LSP package), CLion:** use clangd and pick up
  `compile_commands.json` from the project root without configuration.

clangd also runs the checks in `.clang-tidy` as you type and formats with `.clang-format`.

## clang-tidy and clang-format

`dino init` writes the SDK's own configuration for both:

- **`.clang-tidy`:** clang-analyzer and bugprone checks, plus the portability warnings by name.
  Run it from the command line against the same database:

  ```sh
  clang-tidy -p . src/*.c
  ```

- **`.clang-format`:** the SDK's code style. `clang-format -i src/*.c` formats the files, and most
  editors format on save with it.

Both are ordinary files to edit as your project prefers. `dino init` never overwrites them.

## Debugging

Build with `--debug` (`-O0 -g`, not stripped) and debug the binary in `build/<target>/`:

```sh
dino build --debug
gdb build/x86_64/hello
```

For a foreign Linux target, qemu-user can wait for a debugger:

```sh
dino build arm64 --debug
qemu-aarch64 -g 1234 build/arm64/hello &
gdb-multiarch -ex 'target remote :1234' build/arm64/hello
```

Windows builds can be debugged under wine with `winedbg`, or with gdb on a real Windows machine.
