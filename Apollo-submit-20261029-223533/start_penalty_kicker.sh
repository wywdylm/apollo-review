#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Explicit single-agent decision mode; real-server penalty validation pending.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export APOLLO_MODE=penalty-kicker
export APOLLO_PLAYER_NUMBERS="${PENALTY_KICKER_NUMBER:-7}"
[[ "$APOLLO_PLAYER_NUMBERS" =~ ^[1-7]$ ]] || { echo "Penalty entry must select exactly one player (1..7)" >&2; exit 1; }
exec bash "$root/start.sh" "$@"
