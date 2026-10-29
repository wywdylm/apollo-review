#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/team_processes.sh"
# Internal selector used by penalty entry points; normal start still launches 7.
read -r -a players <<< "${APOLLO_PLAYER_NUMBERS:-1 2 3 4 5 6 7}"
[[ ${#players[@]} -gt 0 ]] || { echo "No player selected" >&2; exit 1; }
mode="${APOLLO_MODE:-match}"
case "$mode" in
  match|penalty-kicker|penalty-goalie) ;;
  *) echo "Invalid mode: $mode" >&2; exit 1 ;;
esac
if [[ "$mode" != match && ${#players[@]} != 1 ]]; then
  echo "Penalty mode requires exactly one player" >&2; exit 1
fi
declare -A selected=()
for player in "${players[@]}"; do
  [[ "$player" =~ ^[1-7]$ ]] || { echo "Invalid player number: $player (expected 1..7)" >&2; exit 1; }
  [[ -z "${selected[$player]:-}" ]] || { echo "Duplicate player number: $player" >&2; exit 1; }
  selected[$player]=1
done
exec 9>"$runtime_dir/$instance_id.lock"
flock -n 9 || { echo "Another start/stop operation is running for $team_name" >&2; exit 1; }
if [[ -f "$pid_file" ]]; then
  while read -r pid token; do
    if is_owned_player "$pid" "$token"; then
      echo "Team $team_name is already running; stop it with this copy's kill.sh" >&2
      exit 1
    fi
  done < "$pid_file"
fi
binary="$repo_root/build/ApolloCodeBase"
[[ ! -x "$repo_root/ApolloCodeBase" ]] || binary="$repo_root/ApolloCodeBase"
[[ -x "$binary" ]] || { echo "Build ApolloCodeBase first" >&2; exit 1; }
export LD_LIBRARY_PATH="$repo_root/libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$log_dir"
: > "$pid_file"
host="${1:-127.0.0.1}"
port="${2:-60000}"
for i in "${players[@]}"; do
  token="$(cat /proc/sys/kernel/random/uuid)"
  APOLLO_PROCESS_TOKEN="$token" "$binary" --team "$team_name" --player-number "$i" \
    --log-level "${APOLLO_LOG_LEVEL:-info}" --mode "$mode" --host "$host" --port "$port" --asset-root "$repo_root/assets" \
    >"$log_dir/player_${i}.log" 2>&1 9>&- &
  echo "$! $token" >> "$pid_file"
done
echo "Launched $team_name (${#players[@]} processes). Connection success must be checked in server/player logs."
echo "Logs: $log_dir"
