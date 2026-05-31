#!/usr/bin/env bash
## @file package_mac_dmg.sh
## @brief Packages QMud.app into a macOS-mountable disk image from Linux.

set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: package_mac_dmg.sh /path/to/QMud.app /path/to/QMud.dmg" >&2
  exit 2
fi

APP_BUNDLE=$1
DMG_OUTPUT=$2
VOLUME_NAME=${QMUD_DMG_VOLUME_NAME:-QMud}

if [ ! -d "$APP_BUNDLE/Contents" ]; then
  echo "Error: app bundle is missing or invalid: $APP_BUNDLE" >&2
  exit 1
fi

if ! command -v mkfs.hfsplus >/dev/null 2>&1; then
  echo "Error: mkfs.hfsplus is required to create the macOS HFS+ image." >&2
  exit 1
fi
if ! command -v hfsplus >/dev/null 2>&1; then
  echo "Error: hfsplus from libdmg-hfsplus is required to populate the macOS DMG image." >&2
  exit 1
fi
if ! command -v dmg >/dev/null 2>&1; then
  echo "Error: dmg from libdmg-hfsplus is required to compress the macOS DMG image." >&2
  exit 1
fi

OUTPUT_DIR=$(dirname "$DMG_OUTPUT")
STAGE_DIR="$OUTPUT_DIR/.qmud-dmg-stage"
HFS_IMAGE="$OUTPUT_DIR/.qmud-dmg-stage.hfs"

cleanup() {
  rm -rf "$STAGE_DIR"
  rm -f "$HFS_IMAGE"
}
trap cleanup EXIT

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
cp -a "$APP_BUNDLE" "$STAGE_DIR/"

STAGE_SIZE_KIB=$(du -sk "$STAGE_DIR" | awk '{print $1}')
HFS_SIZE_MIB=$(((STAGE_SIZE_KIB * 102 + 99999) / 100000))
if [ "$HFS_SIZE_MIB" -lt 1 ]; then
  HFS_SIZE_MIB=1
fi

rm -f "$DMG_OUTPUT" "$HFS_IMAGE"
dd if=/dev/zero of="$HFS_IMAGE" bs=1M count="$HFS_SIZE_MIB" status=none
mkfs.hfsplus -v "$VOLUME_NAME" "$HFS_IMAGE"
hfsplus "$HFS_IMAGE" --symlinks clone_link addall "$STAGE_DIR"
hfsplus "$HFS_IMAGE" symlink /Applications /Applications
dmg build "$HFS_IMAGE" "$DMG_OUTPUT" --compression lzma --level 5 --run-sectors 2048

if [ ! -s "$DMG_OUTPUT" ]; then
  echo "Error: DMG image was not created: $DMG_OUTPUT" >&2
  exit 1
fi
