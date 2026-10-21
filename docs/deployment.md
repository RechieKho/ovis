# Deployment: release packages and ovisup

How Ovis reaches people who don't build it: the release packages, the
workflow that makes them, and `ovisup`, which installs, updates and removes
them the way rustup does for Rust. The rest of the design is in
[architecture-spec.md](architecture-spec.md).

## Installing

```sh
curl --proto '=https' --tlsv1.2 -sSf \
  https://raw.githubusercontent.com/ovis-lang/ovis/main/tools/ovisup | sh
```

Options go after `sh -s --`: `-y` (don't ask), `--default-toolchain
nightly` (or a version, or `none`), `--no-modify-path`, `--with-model
[3b|7b]` (also download the default model, 2–5 GB). Until the first tagged
release, `stable` has no packages and init installs `nightly` instead
(only when no toolchain was named).

What a system needs: Linux with glibc 2.39 or later (Ubuntu 24.04,
Debian 13, Fedora 40 and newer) and OpenSSL 3, or macOS on arm64; a C
compiler to link programs (`cc`, `clang` or `gcc`; on macOS `xcode-select
--install`); `curl` or `wget`, `tar`, and `sha256sum` or `shasum`.
`ovisup check` says what is missing. Other systems build from source
(README).

## Commands

| Command | Does |
| --- | --- |
| `ovisup init [options]` | the set-up the install script runs (also `ovisup -y …`) |
| `ovisup update [TOOLCHAIN…]` | updates `stable` and `nightly` (versions are pinned), then ovisup itself (`--no-self-update`) |
| `ovisup install TOOLCHAIN…` | `stable`, `nightly` or `0.1.0`; `--force` installs again |
| `ovisup uninstall TOOLCHAIN…` | removes toolchains (also `toolchain uninstall`) |
| `ovisup default [TOOLCHAIN]` | shows or sets what `ovis` runs, installing it if needed |
| `ovisup list`, `show` | the toolchains and their versions; where things are |
| `ovisup which [T]`, `prefix [T]` | a toolchain's `ovis`; its directory, for `CMAKE_PREFIX_PATH` |
| `ovisup check` | C compiler, downloader, PATH, toolchains that run, a model |
| `ovisup self update` | replaces ovisup with the one in the default toolchain's release |
| `ovisup self uninstall` | removes `~/.ovis` and the PATH lines; asks about the model cache (`--purge`, `--keep-models`, `-y`) |

`ovis +nightly build x.ovis` runs another toolchain once; `OVIS_TOOLCHAIN`
chooses one for a shell.

## Layout

```text
~/.ovis/                    OVIS_HOME
  bin/ovis                  shim: +T, $OVIS_TOOLCHAIN or settings → exec the toolchain's
  bin/ovisup                this script
  env                       sourced by shell profiles: puts bin/ on PATH once
  settings                  default=stable
  toolchains/stable/        an unpacked package (below), and .sha256 of it
  toolchains/nightly/
  toolchains/0.1.0/
~/.cache/ovis/models/       models (ovis model pull), shared by all toolchains
```

The shim `exec`s the real `ovis`, so its own path is the toolchain's: the
runtime, the bundled libraries and a model placed beside it are found from
there, and the model daemon it starts is that toolchain's.

Shell profiles: `~/.profile` always, `.bash_profile`, `.bash_login` and
`.bashrc` where they exist, `.zshenv` where zsh is, each with one line
`. "~/.ovis/env" # added by ovisup` on a line of its own; fish gets
`~/.config/fish/conf.d/ovisup.fish`. `self uninstall` removes exactly the
lines with that marker, so a profile is given back as it was.

## Packages

`tools/package.sh BUILD_DIR OUTPUT_DIR` makes `ovis-TARGET.tar.gz` and
`ovis-TARGET.tar.gz.sha256`, TARGET one of `linux-x86_64`,
`linux-aarch64`, `macos-aarch64`. The build is configured with
`-DOVIS_PORTABLE=ON` (no CPU tuning) and `-DOVIS_RELOCATABLE=ON`. A
package unpacks to `ovis/`:

```text
ovis/bin/ovis               RPATH $ORIGIN/../lib (@executable_path/../lib on macOS)
ovis/lib/libLLVM…, libclang…, and the libraries they need that a system may lack
ovis/lib/clang/N/include    libclang's own headers (stddef.h …)
ovis/lib/cmake/Ovis/        find_package(Ovis)
ovis/share/ovis/ovis_runtime.c
ovis/VERSION                "ovis 0.1.0 (COMMIT DATE)", what ovisup shows
ovis/TARGET
```

- **Linux:** every library `ldd` lists is copied except the system's own
  (glibc's, `libgcc_s`, `libstdc++`, OpenSSL, which needs the system's
  certificates, and zlib). The executable has an RPATH, not a RUNPATH
  (`--disable-new-dtags`), because the loader uses an RPATH for the
  bundled libraries' own dependencies too.
- **macOS:** every library outside `/usr/lib` and `/System` that `otool -L`
  reaches is copied, renamed `@rpath/NAME`, given `@loader_path` as an
  rpath, and signed again (`codesign -s -`; arm64 runs only signed code).
- **libclang's headers:** `c_import.cpp` passes `-resource-dir` with
  `bin/../lib/clang/LLVM_MAJOR` when it holds `include/stddef.h`, before
  the build machine's path and, on macOS, `clang -print-resource-dir`.

A package is about 70 MB (LLVM 118 MB and libclang 33 MB unpacked).

## Releases

`.github/workflows/release.yml` packages the three targets, then for each
installs the package with ovisup from a folder laid out as a release, on a
machine without LLVM (Linux: fresh `ubuntu:24.04`, `debian:13` and
`fedora:latest` containers with only a C compiler and zlib; macOS: after
`brew uninstall llvm`), runs examples (`hello`, `squeeze` with zlib.h,
`stack`) and uninstalls. Then:

- a tag `vX.Y.Z` (it must match `project(VERSION)` in CMakeLists.txt, which
  `ovis --version` prints) publishes the release `vX.Y.Z`;
- a push to `main` replaces the assets of the `nightly` pre-release and
  moves its tag.

Each release holds `ovis-TARGET.tar.gz`, its `.sha256` and `ovisup`.
ovisup downloads from

| Toolchain | URL |
| --- | --- |
| `stable` | `https://github.com/ovis-lang/ovis/releases/latest/download/FILE` |
| `nightly` | `…/releases/download/nightly/FILE` |
| `0.1.0` | `…/releases/download/v0.1.0/FILE` |

`OVISUP_DIST_SERVER=URL` replaces these with `URL/latest/FILE`,
`URL/nightly/FILE` and `URL/vX.Y.Z/FILE` (`file://` too: the tests and
the release workflow use a folder).

## How ovisup installs a toolchain

1. Download `FILE.sha256`. Not found (curl 22/37, wget 8) means the
   release has no package for this target; other failures stop.
2. If the installed toolchain's `.sha256` is the same, it is unchanged.
3. Download the package, check its SHA-256, unpack it in a temporary
   directory and run `ovis --version` there: a package that doesn't run
   on this system (an older glibc, a missing library) is refused with the
   loader's message.
4. Move the old toolchain aside, the new one in, then delete the old one:
   a failed update leaves the old toolchain working.

## Tests

`test/ovisup_test.cpp` (`[ovisup]`) runs `tools/ovisup` against a folder of
releases whose `ovis` is a small script: set-up with the nightly fallback,
the shim with `+T` and `OVIS_TOOLCHAIN`, profiles changed once and given
back byte for byte, `default`, `which`, updates that keep unchanged and
pinned toolchains, damaged and non-running packages refused with the old
toolchain kept, self update, self uninstall with and without the models.
The real packages are tested by the release workflow (above).
