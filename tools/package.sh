#!/bin/sh
# Makes a release package of ovis that runs without LLVM installed: the
# installed component (bin/ovis, the runtime's C source, the CMake package),
# the shared libraries it needs that a system may lack (LLVM, libclang and
# theirs) in lib/, and libclang's own headers (stddef.h) in lib/clang/N.
# ovisup (tools/ovisup) installs these packages; docs/deployment.md.
#
#   tools/package.sh BUILD_DIR OUTPUT_DIR
#
# BUILD_DIR is configured with -DOVIS_RELOCATABLE=ON (and -DOVIS_PORTABLE=ON
# for a binary meant for other machines). Writes OUTPUT_DIR/ovis-TARGET.tar.gz
# and its .sha256, and prints the package's path.
set -eu

if [ $# -ne 2 ]; then
  echo "usage: tools/package.sh BUILD_DIR OUTPUT_DIR" >&2
  exit 2
fi
build=$1
output=$2
source_dir=$(cd "$(dirname "$0")/.." && pwd)

case "$(uname -s)" in
  Linux) os=linux ;;
  Darwin) os=macos ;;
  *) echo "package.sh: unsupported system $(uname -s)" >&2; exit 1 ;;
esac
case "$(uname -m)" in
  x86_64 | amd64) arch=x86_64 ;;
  aarch64 | arm64) arch=aarch64 ;;
  *) echo "package.sh: unsupported machine $(uname -m)" >&2; exit 1 ;;
esac
target="$os-$arch"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
stage="$work/ovis"
cmake --install "$build" --component ovis --prefix "$stage" > /dev/null
mkdir -p "$stage/lib"

# Libraries every system of the target has, or that must come from it
# (glibc's, the C++ runtime, OpenSSL with the system's certificates). The
# rest is copied.
is_system_library() {
  case "$1" in
    linux-vdso.so* | ld-linux* | libc.so* | libm.so* | libdl.so* | \
    libpthread.so* | librt.so* | libutil.so* | libgcc_s.so* | \
    libstdc++.so* | libssl.so* | libcrypto.so* | libz.so*) return 0 ;;
  esac
  return 1
}

libclang=""
if [ "$os" = linux ]; then
  # ldd lists the whole closure, and the executable's RPATH ($ORIGIN/../lib,
  # OVIS_RELOCATABLE) is used for the libraries' own dependencies too.
  libclang=$(ldd "$stage/bin/ovis" | awk '$1 ~ /^libclang/ { print $3; exit }')
  ldd "$stage/bin/ovis" | while read -r name arrow path rest; do
    [ "$arrow" = "=>" ] || continue
    is_system_library "$name" && continue
    case "$path" in /*) ;; *) echo "package.sh: $name not found" >&2; exit 1 ;; esac
    cp -L "$path" "$stage/lib/$name"
  done
else
  # Copies every library outside the OS that ovis reaches, names each by
  # @rpath, and lets the copies find each other (@loader_path).
  llvm_lib=${OVIS_LLVM_LIBDIR:-$(brew --prefix llvm)/lib}
  resolve() { # REFERENCE FROM_FILE
    case "$1" in
      @rpath/* | @loader_path/* | @executable_path/*)
        base=${1#*/}
        for candidate in "$(dirname "$2")/$base" "$llvm_lib/$base"; do
          [ -f "$candidate" ] && { echo "$candidate"; return; }
        done
        echo "package.sh: cannot find $1 (from $2)" >&2; exit 1 ;;
      *) echo "$1" ;;
    esac
  }
  # A queue of (copied file, its original) pairs whose references to follow.
  queue="$stage/bin/ovis|$(cd "$build" && pwd)/ovis"
  while [ -n "$queue" ]; do
    entry=${queue%%
*}
    if [ "$entry" = "$queue" ]; then queue=""; else queue=${queue#*
}; fi
    file=${entry%%|*}
    original=${entry#*|}
    for reference in $(otool -L "$file" | tail -n +2 | awk '{ print $1 }'); do
      case "$reference" in /usr/lib/* | /System/*) continue ;; esac
      name=$(basename "$reference")
      [ "$name" = "$(basename "$file")" ] && continue
      install_name_tool -change "$reference" "@rpath/$name" "$file"
      if [ ! -f "$stage/lib/$name" ]; then
        found=$(resolve "$reference" "$original")
        cp -L "$found" "$stage/lib/$name"
        chmod u+w "$stage/lib/$name"
        install_name_tool -id "@rpath/$name" "$stage/lib/$name"
        install_name_tool -add_rpath @loader_path "$stage/lib/$name" 2> /dev/null || true
        queue="$queue${queue:+
}$stage/lib/$name|$found"
        case "$name" in libclang*) libclang=$found ;; esac
      fi
    done
  done
  # Changed files lose their signature, and arm64 runs only signed code.
  for file in "$stage/bin/ovis" "$stage"/lib/*.dylib; do
    codesign --force --sign - "$file" 2> /dev/null
  done
fi

# libclang's own headers: beside the libclang CMake found (where the build
# found them too), else beside the one that was bundled.
if [ -n "$libclang" ]; then
  configured=$(sed -n 's/^LIBCLANG_LIBRARY:FILEPATH=//p' "$build/CMakeCache.txt")
  resources=${OVIS_CLANG_RESOURCE_DIR:+$OVIS_CLANG_RESOURCE_DIR/include}
  for library in "$configured" "$libclang"; do
    [ -z "$resources" ] && [ -n "$library" ] || continue
    for directory in "$(dirname "$library")" \
      "$(dirname "$(realpath "$library" 2> /dev/null || echo "$library")")"; do
      found=$(ls -d "$directory"/clang/*/include 2> /dev/null | tail -n 1)
      [ -n "$found" ] && { resources=$found; break; }
    done
  done
  if [ -z "$resources" ] || [ ! -f "$resources/stddef.h" ]; then
    echo "package.sh: libclang's headers (clang/N/include) not found; set OVIS_CLANG_RESOURCE_DIR" >&2
    exit 1
  fi
  version=$(basename "$(dirname "$resources")")
  mkdir -p "$stage/lib/clang/$version"
  cp -R "$resources" "$stage/lib/clang/$version/include"
fi

# What ovisup shows for this package.
commit=$(git -C "$source_dir" rev-parse --short HEAD 2> /dev/null || echo unknown)
release=$("$stage/bin/ovis" --version)
echo "$release ($commit $(date -u +%Y-%m-%d))" > "$stage/VERSION"
echo "$target" > "$stage/TARGET"

mkdir -p "$output"
archive="$output/ovis-$target.tar.gz"
tar -C "$work" -czf "$archive" ovis
(
  cd "$output"
  if command -v sha256sum > /dev/null; then
    sha256sum "ovis-$target.tar.gz"
  else
    shasum -a 256 "ovis-$target.tar.gz"
  fi
) > "$archive.sha256"
echo "$archive"
