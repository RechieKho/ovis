# History: deployment, projects, packages

Archived from state.md → "History" on 2026-10-22 (dream at 979d641).

| Commit | Summary |
| --- | --- |
| (P2) | **Packages H1–H9** ([projects.md](projects.md)): `packages.cpp` (tags, cache, tree hashes, MVS, `combined_c_dependencies`); front end: `phrase_file::locked`/`sees`/`aliases`/`package_label`, `load_package_files`, `vocabulary::sees`/`core_suffix`/qualifiers, `package_core_suffix`, the orphan rule, a package's trait implemented elsewhere; `modules.cpp` finds values in their own package; `record_print_names`; `ovis add`/`remove`/`tree`/`fetch`/`vendor`/`release`, `update` for packages; build/ and vendor/ not modules |
| (P2) | **Projects J1–J8** ([projects.md](projects.md)): `src/ovis/project/` (`toml`, `project` with `[package]`/`[dependencies]`/`[overrides]` read for H1, `native`, `cmake_project`, `lock`); `apply_project` in `main.cpp`; `ovis new`, `init`, `cmake`, `update`; `--link-flags` and `ovis_add_executable(… USE_LINKS)`, `OVIS_FLAGS`; OvisMacros embedded (`embed_text.cmake` takes `HEADER`, `NAMESPACE`); the shim's `project_toolchain`, `ovisup shim`; `examples/project` in CI |
| (P2) | **Deployment** ([deployment.md](deployment.md)): `tools/ovisup` (POSIX sh, like rustup: `init`, `update`, `install`/`uninstall` toolchains `stable`/`nightly`/versions, `default`, `list`, `show`, `which`, `prefix`, `check`, `self update`, `self uninstall`; a shim `ovis` with `+T` and `OVIS_TOOLCHAIN`; checksums; profile lines given back exactly); `tools/package.sh` and `-DOVIS_RELOCATABLE=ON` (RPATH `$ORIGIN/../lib`, LLVM, libclang and their libraries bundled, clang's headers found beside `ovis`); `.github/workflows/release.yml` (tags and nightly, each package tried without LLVM); `ovis --version`; `ovisup_test` |
