#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEAM_NAME="${OPPONENT_TEAM_NAME:-Opponent}" bash "$repo_root/kill.sh"
