#!/bin/sh
# Build and run the host-side tests against nyx-lib's public headers
# (NYX_LIB_INCLUDE, default ../nyx-lib/include/public).
set -e
here=$(cd "$(dirname "$0")" && pwd)
nyxinc=${NYX_LIB_INCLUDE:-$here/../../nyx-lib/include/public}
build=${BUILD_DIR:-/tmp}
mkdir -p "$build/nyxgen/nyx/common"
sed -e 's/@NYX_API_VERSION_MAJOR@/7/' -e 's/@NYX_API_VERSION_MINOR@/0/' \
    "$nyxinc/nyx/common/nyx_version.h.in" > "$build/nyxgen/nyx/common/nyx_version.h"
${CC:-cc} -std=gnu99 -Wall -I"$build/nyxgen" -I"$nyxinc" \
    $(pkg-config --cflags glib-2.0) "$here/test_charger_eval.c" \
    $(pkg-config --libs glib-2.0) -o "$build/test_charger_eval"
"$build/test_charger_eval" "$@"
