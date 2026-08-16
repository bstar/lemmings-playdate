#!/bin/sh
# Provide a pinned Playdate SDK under build/reference/playdate-sdk.
#
# An SDK supplied by the caller (PLAYDATE_SDK_ROOT, normally PLAYDATE_SDK_PATH
# from the Makefile) is used as-is. Only the managed location is downloaded.
set -eu

version=3.1.1
expected=2b64574e5640a13fe5645944f8b5169eb5f4e4cce043482ddc1d6c4735e1156b
url=https://download-cdn.panic.com/playdate_sdk/Linux/PlaydateSDK-$version.tar.gz

managed=build/reference/playdate-sdk
root=${PLAYDATE_SDK_ROOT:-$managed}
download_root=build/reference/playdate-sdk-download
archive="$download_root/PlaydateSDK-$version.tar.gz"

if [ -x "$root/bin/pdc" ]; then
    exit 0
fi

if [ "$root" != "$managed" ] && [ "$root" != "$PWD/$managed" ]; then
    echo "no Playdate SDK at $root" >&2
    echo "install it there, or unset PLAYDATE_SDK_PATH to use the managed download" >&2
    exit 1
fi

if [ "$(uname -s)" != Linux ]; then
    echo "the managed SDK download is Linux-only" >&2
    echo "install the SDK and pass PLAYDATE_SDK_PATH=/path/to/PlaydateSDK" >&2
    exit 1
fi

if [ -e "$root" ]; then
    echo "existing Playdate SDK at $root is incomplete; remove it and retry" >&2
    exit 1
fi

mkdir -p "$download_root"
if [ ! -f "$archive" ]; then
    curl -L --fail --output "$archive" "$url"
fi
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$archive" | cut -d ' ' -f 1)
else
    actual=$(shasum -a 256 "$archive" | cut -d ' ' -f 1)
fi
if [ "$actual" != "$expected" ]; then
    echo "Playdate SDK checksum mismatch: $actual" >&2
    exit 1
fi

temporary=$(mktemp -d build/reference/playdate-sdk.XXXXXX)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
tar -xzf "$archive" -C "$temporary" --strip-components=1
mv "$temporary" "$root"
trap - EXIT HUP INT TERM
echo "installed Playdate SDK $version at $root"
