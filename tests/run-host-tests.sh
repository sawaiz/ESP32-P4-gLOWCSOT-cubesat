#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -Werror -I "$repo/main" "$repo/tests/protocol_test.c" "$repo/main/telemetry_protocol.c" -o "$out/protocol-test"
"$out/protocol-test" "$out/fixture.bin"
app="$repo/MuonMonitor"
if [ -f "$app/Tests/main.swift" ] && command -v swiftc >/dev/null 2>&1; then
    swiftc -module-cache-path "$out/swift-cache" "$app/Shared/Telemetry.swift" "$app/Shared/Correction.swift" "$app/Tests/main.swift" -o "$out/swift-test"
    "$out/swift-test" "$out/fixture.bin"
fi
