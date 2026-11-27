#!/usr/bin/env bash
# tt-mount-card.sh — mount the Tone Trixie SD card (USB drive mode) on macOS, bypassing FSKit.
#
# WHY THIS EXISTS: on macOS 26 (Tahoe), FSKit reads the whole FAT before it auto-mounts, and gives up
# after ~30 s. On the bit-bang SPI build (~133 KB/s) a 31 GB card's ~7.7 MB FAT can't be read in time,
# so Finder never mounts it. `mount -t msdos` uses the legacy driver, which reads the FAT lazily, so this
# script still works. The native SDMMC build (`make SD_SDMMC=1`) is fast enough that the card
# auto-mounts on its own; this is now a FALLBACK for the bit-bang build.
#
# Put the pedal in USB drive mode first: hold the encoder at power-up, or send `drive` over the console
# / the studio's one-click Sync.
#
# Usage:
#   tt-mount-card.sh           # find + mount the TONETRIX card, print its mountpoint + contents
#   tt-mount-card.sh -u        # unmount + eject it (the pedal then auto-returns out of drive mode)
#   tt-mount-card.sh -n NAME   # match a different volume name (default: TONETRIX)
#
# Needs sudo (mount_msdos + making the mountpoint are root-only); you'll be prompted once.
set -euo pipefail

NAME=TONETRIX
ACTION=mount
while getopts "un:" opt; do
  case "$opt" in
    u) ACTION=eject ;;
    n) NAME="$OPTARG" ;;
    *) echo "usage: $0 [-u] [-n NAME]" >&2; exit 2 ;;
  esac
done

# Find the FAT32 slice whose volume name matches NAME (single-word names parse cleanly from diskutil list).
slice=$(diskutil list | awk -v n="$NAME" '
  /DOS_FAT_32/ { for (i = 1; i <= NF; i++) if ($i == "DOS_FAT_32" && $(i + 1) == n) print $NF }' | head -1)
# Fallback: the first external FAT32 slice, if the exact name wasn't found.
if [ -z "${slice:-}" ]; then
  slice=$(diskutil list | awk '/DOS_FAT_32/ { print $NF }' | head -1)
fi
if [ -z "${slice:-}" ]; then
  echo "no FAT32 card found — is the pedal on USB and in drive mode? (diskutil list shows no DOS_FAT_32)" >&2
  exit 1
fi

mp="/Volumes/$NAME"

if [ "$ACTION" = eject ]; then
  diskutil eject "$slice" && echo "ejected $slice ($NAME) — the pedal should return out of drive mode"
  exit 0
fi

if mount | grep -q " on $mp "; then
  echo "already mounted: $mp"
  ls "$mp"
  exit 0
fi

echo "mounting /dev/$slice → $mp  (legacy mount_msdos; FSKit auto-mount is bypassed)"
sudo mkdir -p "$mp"
sudo mount -t msdos "/dev/$slice" "$mp"
echo "mounted: $mp"
ls "$mp"
