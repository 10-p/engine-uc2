#!/usr/bin/env bash
# Regenerate the C++ headers Source/<Pkg>/Inc/<Pkg>Classes.h from the script sources: the export
# tools/dev/check-headers.sh does, then the copy over Source/ (CRs dropped). Same options (--build, --classes-root,
# --tree, --logs). T2, docs/porting/analysis/headers.md.
exec "$(dirname "$0")/check-headers.sh" --write "$@"
