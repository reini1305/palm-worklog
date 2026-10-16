#!/bin/sh
# Builds WorkLog.prc using the toolchain container (Docker, OrbStack or Colima).
set -e
cd "$(dirname "$0")"
docker build -q -t palmdev . >/dev/null
docker run --rm --platform linux/amd64 -v "$PWD":/src:Z palmdev make "$@"
