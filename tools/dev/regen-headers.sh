#!/usr/bin/env bash
# Regenerate the C++ headers Source/<Pkg>/Inc/<Pkg>Classes.h from the scripts (default: the build's patched tree,
# <build>/Scripts/tree): the export tools/dev/check-headers.sh does, then the copy into Source/<Pkg>/Inc, the
# headers' destination (CRs dropped; on a patched tree the committed cpptext and native structs are kept). Same
# options (--build, --classes-root, --tree, --logs).
exec "$(dirname "$0")/check-headers.sh" --write "$@"
