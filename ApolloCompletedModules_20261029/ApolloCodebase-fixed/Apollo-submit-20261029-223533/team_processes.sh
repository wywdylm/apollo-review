#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Shared instance identity: project directory + team name, independent of cwd.
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
team_name="${TEAM_NAME:-Apollo3Drelease}"
instance_id="$(printf '%s\n%s' "$repo_root" "$team_name" | sha256sum | cut -c1-24)"
runtime_base="${XDG_RUNTIME_DIR:-/tmp}"
runtime_dir="$runtime_base/apollo-${UID}"
if [[ -L "$runtime_dir" ]]; then
  echo "Refusing symlink runtime directory: $runtime_dir" >&2; exit 1
fi
mkdir -p -m 700 "$runtime_dir"
[[ -O "$runtime_dir" ]] || { echo "Runtime directory is not owned by current user" >&2; exit 1; }
pid_file="$runtime_dir/$instance_id.pids"
log_dir="$repo_root/logs/$instance_id"

is_owned_player() {
  local pid="$1" token="$2"
  [[ "$pid" =~ ^[0-9]+$ && -n "$token" && -r "/proc/$pid/environ" ]] || return 1
  grep -zFxq "APOLLO_PROCESS_TOKEN=$token" "/proc/$pid/environ" 2>/dev/null
}
