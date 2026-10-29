#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/team_processes.sh"
exec 9>"$runtime_dir/$instance_id.lock"
flock -n 9 || { echo "Another start/stop operation is running for $team_name" >&2; exit 1; }
if [[ -f "$pid_file" ]]; then
  while read -r pid token; do
    if is_owned_player "$pid" "$token"; then
      kill "$pid" 2>/dev/null || true
    fi
  done < "$pid_file"
  # Escalate only for the same uniquely tagged process after a short grace period.
  for attempt in $(seq 1 20); do
    alive=0
    while read -r pid token; do
      if is_owned_player "$pid" "$token"; then alive=1; fi
    done < "$pid_file"
    [[ "$alive" == 1 ]] || break
    sleep 0.1
  done
  while read -r pid token; do
    if is_owned_player "$pid" "$token"; then kill -KILL "$pid" 2>/dev/null || true; fi
  done < "$pid_file"
  rm -f "$pid_file"
fi
echo "Stopped only this project copy's team: $team_name"
