#!/usr/bin/env bash
#
# Make `pio run` work inside a Claude Code remote container.
#
# Two things get in the way, both environmental rather than anything to do with WLED:
#
#   1. Outbound TLS is re-terminated by an agent proxy. Several tools carry their own
#      CA bundle and ignore the standard environment variables, so the proxy CA has to
#      be installed into each of them. PlatformIO is one; the espressif32 platform
#      keeps a *second* Python environment with its own bundle, which is the one that
#      trips people up because it only appears after the first platform install.
#
#   2. The PlatformIO package registry (*.registry.platformio.org) is blocked by the
#      network policy. GitHub and PyPI are reachable, so anything normally fetched by
#      registry name has to come from there instead.
#
# Safe to re-run. See README.md in this directory for what to do when it stops.
set -euo pipefail

CA=${CCR_CA_BUNDLE:-/root/.ccr/ca-bundle.crt}
REPO=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
PIO_HOME=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

log() { printf '\n== %s\n' "$*"; }

# --- 1. Trust the proxy CA everywhere PlatformIO might look --------------------------
# Re-run after the platform is installed: the penv bundle does not exist before that.
trust_proxy_ca() {
  [ -f "$CA" ] || return 0
  local bundle
  while read -r bundle; do
    [ -n "$bundle" ] && cp "$CA" "$bundle"
  done < <(find /root/.local/lib /usr/local/lib "$PIO_HOME/penv" \
             -path '*/certifi/cacert.pem' 2>/dev/null || true)
}

export REQUESTS_CA_BUNDLE="$CA" SSL_CERT_FILE="$CA" CURL_CA_BUNDLE="$CA"

log "Installing PlatformIO if needed"
command -v pio >/dev/null || pip3 install --quiet --break-system-packages platformio

trust_proxy_ca
pio settings set enable_telemetry false >/dev/null  # collector.platformio.org is blocked

# --- 2. tool-scons, which only exists in the blocked registry ------------------------
# Build the package from the PyPI sdist. The sdist has no scons.py launcher, so write
# one, and hand-write the .piopm metadata PlatformIO matches the `platformio/tool-scons`
# spec against - without it, it tries the registry regardless of what is on disk.
SCONS_VERSION=4.8.1
SCONS_PIO_VERSION=4.40801.0   # PlatformIO's encoding of the same version
if [ ! -f "$PIO_HOME/packages/tool-scons/scons.py" ]; then
  log "Building tool-scons $SCONS_VERSION from the PyPI sdist"
  ( cd "$WORK"
    pip3 download --no-deps --no-binary :all: "scons==$SCONS_VERSION" -d . >/dev/null
    tar xzf "scons-$SCONS_VERSION.tar.gz"
    DST="$PIO_HOME/packages/tool-scons"
    mkdir -p "$DST"
    cp -r "SCons-$SCONS_VERSION/SCons" "$DST/SCons"
    cat > "$DST/scons.py" <<'PY'
#!/usr/bin/env python
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import SCons.Script  # noqa: E402

SCons.Script.main()
PY
    printf '{"name": "tool-scons", "version": "%s", "license": "MIT"}\n' \
      "$SCONS_PIO_VERSION" > "$DST/package.json"
    printf '{"type": "tool", "name": "tool-scons", "version": "%s", "spec": {"owner": "platformio", "id": null, "name": "tool-scons", "requirements": "~%s", "uri": null}}\n' \
      "$SCONS_PIO_VERSION" "$SCONS_PIO_VERSION" > "$DST/.piopm"
  )
fi

# --- 3. Registry-pinned libraries -> GitHub tags -------------------------------------
if [ ! -f "$REPO/platformio_override.ini" ]; then
  log "Installing platformio_override.ini pinning registry libraries to GitHub"
  cp "$REPO/tools/dev/platformio_override.offline.ini" "$REPO/platformio_override.ini"
else
  echo "platformio_override.ini already exists, leaving it alone"
fi

# --- 4. lib_dir mirror without the one library that pins a registry dependency -------
# lib/NeoESP32RmtHI declares makuna/NeoPixelBus by registry name. The V5 esp32 envs put
# it in lib_ignore anyway, but PlatformIO reads declared dependencies before lib_ignore
# is applied, so it has to be out of lib_dir entirely.
log "Building lib_dir mirror at .pio/liblocal"
MIRROR="$REPO/.pio/liblocal"
rm -rf "$MIRROR"; mkdir -p "$MIRROR"
for d in "$REPO"/lib/*/; do
  n=$(basename "$d")
  [ "$n" = "NeoESP32RmtHI" ] && continue
  ln -sfn "$REPO/lib/$n" "$MIRROR/$n"
done

trust_proxy_ca  # in case a platform (and its penv) was installed by an earlier run

cat <<'EOF'

Setup done. Now:

    npm ci && npm run build
    tools/dev/pio-build.sh <env>          # wraps `pio run` with the dependency fixup

The first build downloads ~1GB of toolchain and takes a while. Use pio-build.sh rather
than `pio run` directly: newly installed libraries re-declare registry dependencies
each time, so it alternates building and stripping until resolution settles.
EOF
