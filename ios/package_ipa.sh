#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 /path/to/th06.app /path/to/th06.ipa" >&2
    exit 2
fi

APP_PATH=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
IPA_PATH=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")

if [ ! -x "$APP_PATH/th06" ] || [ ! -f "$APP_PATH/Info.plist" ]; then
    echo "error: invalid app bundle: $APP_PATH" >&2
    exit 2
fi
track=1
while [ "$track" -le 17 ]; do
    formatted=$(printf '%02d' "$track")
    if [ ! -s "$APP_PATH/bgm/th06_${formatted}.ogg" ]; then
        echo "error: app bundle is missing bgm/th06_${formatted}.ogg" >&2
        exit 2
    fi
    track=$((track + 1))
done
if find "$APP_PATH/bgm" -type f -iname '*.wav' | grep -q .; then
    echo "error: app bundle contains stale WAV BGM; use a clean OGG staging directory" >&2
    exit 2
fi
for archive in CM ED IN MD ST TL; do
    if [ ! -s "$APP_PATH/KOUMAKYO_${archive}.dat" ]; then
        echo "error: app bundle is missing KOUMAKYO_${archive}.dat" >&2
        exit 2
    fi
done

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/th06-ipa.XXXXXX")
trap 'rm -rf "$WORK_DIR"' EXIT INT TERM
mkdir -p "$WORK_DIR/Payload"
cp -R "$APP_PATH" "$WORK_DIR/Payload/th06.app"
rm -f "$IPA_PATH"
(cd "$WORK_DIR" && /usr/bin/zip -qry "$IPA_PATH" Payload)
echo "created $IPA_PATH"
