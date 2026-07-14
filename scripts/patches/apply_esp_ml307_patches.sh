#!/usr/bin/env bash
#
# Re-applies local fixes to the vendored `78/esp-ml307` component.
#
# `managed_components/` is fetched by the ESP-IDF Component Manager and is
# git-ignored, so any hand edits made there are lost whenever the component
# is re-resolved (idf.py fullclean, deleting managed_components/, bumping
# the esp-ml307 version pin in main/idf_component.yml, CI checkout, etc).
#
# Run this script after any such re-fetch (e.g. right after `idf.py build`
# or `idf.py reconfigure` finishes downloading dependencies) to restore the
# fixes. It is safe to run repeatedly: already-patched files are skipped,
# and files that don't match either the known original or the known
# patched content are left untouched with a warning (likely an upstream
# version bump — re-review the fix and update the patch set).
#
# Fixes applied:
#   esp_tcp.{h,cc} - Disconnect()/DoDisconnect() race that could destroy an
#     EspTcp/HttpClient object (freeing its mutex) while the background
#     `tcp_receive` task was still invoking the disconnect callback on it,
#     causing an intermittent `xQueueSemaphoreTake ... uxItemSize == 0`
#     assert and reboot (observed via lotusai.recommend's HTTP requests).
#     See docs/nanabot/lotusai.md and this script's directory for details.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
COMPONENT_DIR="$REPO_ROOT/managed_components/78__esp-ml307/src/esp"

status=0

apply_one() {
    local name="$1"
    local target="$COMPONENT_DIR/$name"
    local orig="$SCRIPT_DIR/esp-ml307/$name.orig"
    local patched="$SCRIPT_DIR/esp-ml307/$name.patched"

    if [ ! -f "$target" ]; then
        echo "SKIP  $name: not found under $COMPONENT_DIR (component not fetched yet; run idf.py build/reconfigure first)"
        return
    fi

    if cmp -s "$target" "$patched"; then
        echo "OK    $name: already patched"
        return
    fi

    if cmp -s "$target" "$orig"; then
        cp "$patched" "$target"
        echo "FIXED $name: applied disconnect-race patch"
        return
    fi

    echo "WARN  $name: content doesn't match the known original or patched version." >&2
    echo "      This usually means the esp-ml307 component version changed." >&2
    echo "      Re-review the fix manually (see $SCRIPT_DIR/esp-ml307/) and update the .orig/.patched references." >&2
    status=1
}

apply_one "esp_tcp.h"
apply_one "esp_tcp.cc"

exit $status
