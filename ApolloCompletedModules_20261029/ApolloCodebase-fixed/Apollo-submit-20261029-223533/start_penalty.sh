#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compatibility entry point for the naming used in the rule table
# ("start_penalty.sh"), which the body text splits into
# start_penalty_kicker.sh / start_penalty_goalie.sh.
#
# Usage:
#   bash start_penalty.sh <server_ip> [port]            # default: kicker
#   APOLLO_PENALTY_ROLE=goalie bash start_penalty.sh <server_ip> [port]
#
# The server IP is the first positional argument, as required by the rules.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

role="${APOLLO_PENALTY_ROLE:-kicker}"
case "$role" in
  kicker|penalty-kicker)
    exec bash "$root/start_penalty_kicker.sh" "$@"
    ;;
  goalie|goalkeeper|penalty-goalie)
    exec bash "$root/start_penalty_goalie.sh" "$@"
    ;;
  *)
    echo "Invalid APOLLO_PENALTY_ROLE: $role (expected kicker or goalie)" >&2
    exit 1
    ;;
esac
