#!/usr/bin/env bash
set -euo pipefail
exec bash "$(dirname "$0")/build_l1_image.sh" "$@"
