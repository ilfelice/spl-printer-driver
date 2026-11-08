#!/bin/sh
# Copies the driver into a Haiku source tree so that it can be built with jam.
#
# Usage: sh install-into-haiku-tree.sh /path/to/haiku
set -e

HAIKU_TOP="$1"
DRIVERS="$HAIKU_TOP/src/add-ons/print/drivers"
if [ -z "$HAIKU_TOP" ] || [ ! -f "$DRIVERS/Jamfile" ]; then
	echo "usage: $0 /path/to/haiku-source-tree" >&2
	exit 1
fi

HERE=$(cd "$(dirname "$0")" && pwd)
TARGET="$DRIVERS/spl"
mkdir -p "$TARGET"

# the Haiku specific part
cp "$HERE/SPLCap.cpp" "$HERE/SPLCap.h" "$HERE/SPLDriver.cpp" \
	"$HERE/SPLDriver.h" "$HERE/SPLEntry.cpp" "$HERE/SPL.rdef" \
	"$HERE/Jamfile" "$TARGET/"

# the portable encoder core
cp "$HERE/../spl.h" "$HERE/../spl_encoder.cpp" \
	"$HERE/../spl_algo0x11.cpp" "$HERE/../spl_models.cpp" \
	"$HERE/../spl_models_generated.inc" "$TARGET/"

if ! grep -q "print drivers spl ;" "$DRIVERS/Jamfile"; then
	echo "SubInclude HAIKU_TOP src add-ons print drivers spl ;" \
		>> "$DRIVERS/Jamfile"
fi

echo "Driver sources installed in $TARGET"
echo "Now build it from your 'generated' directory:  jam -q \"SPL Compatible\""
