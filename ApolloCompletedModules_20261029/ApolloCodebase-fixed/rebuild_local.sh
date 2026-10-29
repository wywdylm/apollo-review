#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
original="${1:-$HOME/robocup/ApolloCodebase-main}"
onnx="${ONNXRUNTIME_ROOT:-$original/deploy/thirdparty/onnxruntime-linux-x64-1.22.0}"
[[ -f "$onnx/include/onnxruntime_cxx_api.h" ]] || { echo "找不到 ONNX Runtime：$onnx；请用 ONNXRUNTIME_ROOT 指定之前安装的位置。" >&2; exit 1; }
cmake -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Release -DONNXRUNTIME_ROOT="$onnx"
cmake --build "$root/build" -j2
