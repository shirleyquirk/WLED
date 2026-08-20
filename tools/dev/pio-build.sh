#!/usr/bin/env bash
#
# `pio run` with the offline dependency fixup applied between attempts.
#
# Each wave of newly installed libraries re-declares dependencies that can only be
# resolved through the blocked registry, so building and stripping have to alternate
# until resolution settles. Usually two or three passes.
set -euo pipefail

ENV_NAME=${1:?usage: pio-build.sh <platformio-env> [extra pio args...]}
shift || true

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
CA=${CCR_CA_BUNDLE:-/root/.ccr/ca-bundle.crt}
[ -f "$CA" ] && export REQUESTS_CA_BUNDLE="$CA" SSL_CERT_FILE="$CA"

cd "$REPO"
for attempt in 1 2 3 4 5; do
  python3 tools/dev/strip-registry-deps.py ".pio/libdeps/$ENV_NAME" >/dev/null 2>&1 || true
  if output=$(pio run -e "$ENV_NAME" "$@" 2>&1); then
    if ! grep -q "HTTPClientError" <<<"$output"; then
      echo "$output" | tail -20
      exit 0
    fi
  fi
  echo "$output" | tail -5
  if ! grep -q "HTTPClientError" <<<"$output"; then
    exit 1   # a real build failure, not a blocked registry lookup
  fi
  echo "-- registry lookup blocked, stripping newly declared dependencies (pass $attempt)"
done

echo "Still hitting the registry after 5 passes; see tools/dev/README.md" >&2
exit 1
