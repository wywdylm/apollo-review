#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 The Hong Kong University of Science and Technology (Guangzhou), Humanoid Computing & Learning Lab.

set -euo pipefail

# Package ApolloCodeBase for deployment
# Usage: pack.sh [destination]
# Output: <destination>/ApolloCodeBase.tar.gz

DEST="${1:-.}"
PACK_NAME="ApolloCodeBase"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$repo_root/build"
binary="$build_dir/ApolloCodeBase"

if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
  echo "Error: $build_dir is not configured. Run cmake -S . -B build first." >&2
  exit 1
fi

cmake --build "$build_dir" -j"$(nproc)"

if [[ ! -x "$binary" ]]; then
  echo "Error: $binary not found after build." >&2
  exit 1
fi

staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT

pkg="$staging/$PACK_NAME"
mkdir -p "$pkg/libs"

cp "$binary" "$pkg/"

cp -r "$repo_root/assets" "$pkg/"

# Non-system shared libraries. yaml-cpp and ONNX Runtime must be packaged
# when they remain dynamic dependencies.
while IFS= read -r line; do
  lib=""
  if [[ "$line" == *"=>"* ]]; then
    lib="$(awk '{print $3}' <<< "$line")"
  else
    lib="$(awk '{print $1}' <<< "$line")"
  fi

  [[ "$lib" == /* && -f "$lib" ]] || continue

  base="$(basename "$lib")"
  case "$base" in
    linux-vdso*|ld-linux*|libc.so*|libm.so*|libpthread.so*|librt.so*|libdl.so*)
      ;;
    libyaml-cpp.so*|libonnxruntime.so*)
      cp -L "$lib" "$pkg/libs/$base"
      ;;
    *)
      case "$lib" in
        /lib/x86_64-linux-gnu/*|/usr/lib/x86_64-linux-gnu/*) ;;  # skip base system libs
        *) cp -L "$lib" "$pkg/libs/$base" ;;
      esac
      ;;
  esac
done < <(ldd "$binary")

ensure_packaged_soname() {
  local soname="$1"

  if [[ -e "$pkg/libs/$soname" ]]; then
    return
  fi

  local lib
  for lib in "$pkg"/libs/*; do
    [[ -e "$lib" ]] || continue
    if [[ "$(readelf -d "$lib" 2>/dev/null | awk -F'[][]' '/SONAME/ {print $2; exit}')" == "$soname" ]]; then
      ln -sf "$(basename "$lib")" "$pkg/libs/$soname"
      return
    fi
  done

  echo "Error: $binary needs $soname but it was not packaged under $pkg/libs." >&2
  exit 1
}

while IFS= read -r needed; do
  case "$needed" in
    libyaml-cpp.so*|libonnxruntime.so*) ensure_packaged_soname "$needed" ;;
  esac
done < <(readelf -d "$binary" | awk -F'[][]' '/NEEDED/ {print $2}')

missing="$(LD_LIBRARY_PATH="$pkg/libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$pkg/ApolloCodeBase" | awk '/not found/')"
if [[ -n "$missing" ]]; then
  echo "Error: packaged binary still has unresolved shared libraries:" >&2
  echo "$missing" >&2
  exit 1
fi

# These scripts detect the packaged executable and use absolute asset paths.
for script in start.sh start_opponent.sh start_penalty.sh start_penalty_kicker.sh start_penalty_goalie.sh kill.sh kill_opponent.sh team_processes.sh; do
  cp "$repo_root/$script" "$pkg/$script"
  chmod +x "$pkg/$script"
done

# Pack
mkdir -p "$DEST"
tar -czf "$DEST/$PACK_NAME.tar.gz" -C "$staging" "$PACK_NAME"
echo "Packaged to $DEST/$PACK_NAME.tar.gz"
